#include <fstream>
#include <regex>

template<class Set>
static void TestOptionConsumers(Set set) {
    const auto factory=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"option fonts");
    {
        PresentationFixture::Tab tab(L"option-consumers",150);
        auto& state=tab.state;
        FocusFixture::Session focus(tab.frame,state.hwnd);
        for(int row:{24,48,96}) for(int listFont:{20,52,80}) for(int gridFont:{20,42,80}) {
            set(L"Appearance.rowHeight",std::to_wstring(row));
            set(L"Appearance.fontSizePercent",std::to_wstring(listFont));
            set(L"Appearance.gridFontSizePercent",std::to_wstring(gridFont));
            for(UINT mode:{FVM_DETAILS,FVM_LIST,FVM_ICON,FVM_SMALLICON}) {
                ApplyViewLayout(state,{mode,48},false); UpdateMetrics(state);
                const int expected=std::max(7,MulDiv(Scale96(row,ViewDpi(state)),
                    state.gridView ? gridFont : listFont,100));
                Check(state.fontPx==expected && (state.gridView || state.rowPx==Scale96(row,ViewDpi(state))),
                    "independent list/grid font setting did not reach layout metrics");
                Check(MakeTextFormats(state,true) && state.textFormat->GetFontSize()==expected,
                    "view switch left the previous layout's font in DirectWrite");
            }
        }
        // Caption size remains bounded at very large image zoom, independently
        // of the list font preference and at each tested density.
        state.presentationOnly=true; state.rowHeight96[true]=384;
        for(int dpi:{96,144,192}) for(int gridFont:{20,42,80}) {
            state.presentationDpi=dpi;
            set(L"Appearance.gridFontSizePercent",std::to_wstring(gridFont)); UpdateMetrics(state);
            Check(state.fontPx==std::max(7,MulDiv(Scale96(96,dpi),gridFont,100)),
                "large grid zoom bypassed its independent caption setting or font cap");
        }
        state.presentationOnly=false; state.rowHeight96[true]=0;
        ApplyViewLayout(state,{FVM_DETAILS,48},false);
        set(L"Appearance.rowHeight",L"32"); set(L"Appearance.fontSizePercent",L"52");
        set(L"Appearance.gridFontSizePercent",L"42"); UpdateMetrics(state);
        set(L"Appearance.fontFamily",L"Segoe UI"); Check(MakeTextFormats(state),"configured font formats");
        for(auto* format:{state.textFormat.Get(),state.headerFormat.Get(),state.sortedHeaderFormat.Get()}) {
            std::wstring family(format->GetFontFamilyNameLength()+1,L'\0');
            Check(SUCCEEDED(format->GetFontFamilyName(family.data(),UINT32(family.size()))) &&
                _wcsicmp(family.c_str(),L"Segoe UI")==0,"configured font did not reach body/header/sorted header");
        }
        for(bool rtl:{false,true}) for(int margin:{0,10,70}) {
            state.rtlLayout=rtl; set(L"Appearance.edgeMargin",std::to_wstring(margin));
            Check(ListStartX(state)==Scale96(margin,ViewDpi(state)),"side margin did not reach shared geometry");
        }
        state.rtlLayout=false; set(L"Appearance.edgeMargin",L"10");
        for(bool grid:{false,true}) for(int radius:{-1,0,8,4096}) {
            state.gridView=grid;
            set(grid ? L"Selection.gridSelectionRadius" : L"Selection.selectionRadius",std::to_wstring(radius));
            Check(SelectionRadius(state,100,30)==(radius==-1 ? 15.f : std::min(15.f,float(Scale96(radius,ViewDpi(state))))),
                "selection radius did not use the layout's setting or pill/size cap");
        }
        state.gridView=false;
        for(bool enabled:{false,true}) {
            const auto value=enabled ? L"1" : L"0";
            set(L"Appearance.dimDotFiles",value); Entry item; item.name=L".hidden";
            Check(ItemContentOpacity(state,item)==(enabled ? kDimmedItemContentOpacity : 1.f),"dotfile toggle did not reach content opacity");
            set(L"Appearance.listThumbnails",value); item.folder=false;
            Check(UsesThumbnails(state,item)==enabled,"list thumbnail toggle did not reach image demand");
            item.folder=true; Check(!UsesThumbnails(state,item),"thumbnail option requested folder thumbnails");
            set(L"Hover.animateHover",value); state.hovered=-1; BeginHoverTransition(state,0,true);
            Check(state.hoverAnimationActive==enabled,"hover animation toggle did not reach transition creation"); StopHoverAnimation(state);
            set(L"Navigation.showScrollbar",value); UpdateColumns(state);
            Check(Scrollbar(state).usable==enabled,"scrollbar toggle did not reach geometry/hit testing");
            set(L"Navigation.smoothScrolling",value); SetScrollImmediate(state,0); MoveScroll(state,200);
            Check(state.scrollTarget==200 && state.animationActive==enabled && (enabled || state.scrollNow==200),
                "smooth scrolling toggle did not change viewport motion");
            StopScrollAnimation(state);
        }
        set(L"Navigation.showScrollbar",L"1"); set(L"Navigation.smoothScrolling",L"1"); UpdateColumns(state);
        for(bool instant:{false,true}) {
            set(L"Navigation.instantScrollbarScrub",instant ? L"1" : L"0"); SetScrollImmediate(state,0);
            const auto bar=Scrollbar(state); state.scrollbarGrabY=bar.thumbLength*.5f;
            ScrubScrollbar(state,bar.top+state.scrollbarGrabY+bar.travel*.31f);
            Check(state.scrollTarget>0 && state.animationActive==!instant,"vertical scrub ignored instant/smooth setting");
            Check(!instant || state.scrollNow==state.scrollTarget,"instant vertical scrub retained interpolation");
            StopScrollAnimation(state);
        }
        for(int opacity:{0,35,100}) {
            set(L"Selection.selectionOpacity",std::to_wstring(opacity));
            Check(std::abs(SelectionBaseOpacity(state,{},0,0,100)-opacity/100.f)<.001f,"selection opacity did not reach highlights");
            set(L"Hover.hoverOpacity",std::to_wstring(opacity));
            state.selectedFlags.assign(state.total,0);
            Check(std::abs(HoverOverlayOpacity(state,{0,0,100,1},{})-opacity/100.f)<.001f,"hover opacity did not reach highlights");
        }
        set(L"Selection.selectionOpacity",L"60");
        for(int opacity:{10,60,100}) {
            set(L"Hover.hoverSelectedOpacity",std::to_wstring(opacity));
            Check(std::abs(SelectionBaseOpacity(state,{0,0,100,1},0,0,100)-std::min(.6f,opacity/100.f))<.001f,
                "combined hover opacity did not reach selection blending");
        }
    }
    ShutDownFrames(); g_frameStopping=false; g_writeFactory=factory;
}

// Independent expected contract: a newly exposed/renamed/removed option fails
// until this table and its tests are deliberately updated. Read the selected
// source, including historical negative controls, rather than latest.cpp.
static void TestUserOptions() {
    struct Integer {
        const wchar_t* key; int initial,minimum,maximum; std::atomic<int>* setting;
    };
    const Integer integers[]={
        {L"Appearance.rowHeight",32,24,96,&g_row96},
        {L"Appearance.fontSizePercent",52,20,80,&g_fontPercent},
        {L"Appearance.gridFontSizePercent",42,20,80,&g_gridFontPercent},
        {L"Appearance.secondaryTextOpacity",45,0,100,&g_secondaryOpacity},
        {L"Appearance.zebraOpacity",4,0,100,&g_zebraOpacity},
        {L"Appearance.edgeMargin",10,0,400,&g_edgeMargin96},
        {L"Selection.selectionOpacity",60,0,100,&g_selectionAlpha},
        {L"Selection.selectionRadius",8,-1,4096,&g_selectionRadius},
        {L"Selection.gridSelectionRadius",16,-1,4096,&g_gridSelectionRadius},
        {L"Selection.marqueeOpacity",30,0,100,&g_marqueeAlpha},
        {L"Hover.hoverOpacity",50,0,100,&g_hoverAlpha},
        {L"Hover.hoverSelectedOpacity",100,0,100,&g_bothAlpha},
    };
    struct Boolean {const wchar_t* key; bool initial; std::atomic<bool>* setting;};
    const Boolean booleans[]={
        {L"Appearance.dimDotFiles",true,&g_dimDotFiles},
        {L"Appearance.listThumbnails",false,&g_listThumbnails},
        {L"Appearance.zebraStripes",true,&g_zebra},
        {L"Appearance.showEmptyRows",false,&g_emptyRows},
        {L"Hover.animateHover",true,&g_animateHover},
        {L"Navigation.smoothScrolling",true,&g_smooth},
        {L"Navigation.showNativeViewToggleButton",true,&g_showNativeViewToggleButton},
        {L"Navigation.touchpadDirectManipulation",true,&g_touchpadDirectManipulation},
        {L"Navigation.touchpadSwipeNavigation",false,&g_touchpadSwipeNavigation},
        {L"Navigation.showScrollbar",true,&g_scrollbar},
        {L"Navigation.instantScrollbarScrub",false,&g_instantScrub},
        {L"Columns.autoRefitColumns",true,&g_autoRefitColumns},
        {L"Columns.preSort",true,&g_preSort},
        {L"enableLogging",false,&g_perf},
    };
    std::map<std::wstring,std::wstring> expected;
    for(const auto& option:integers) expected[option.key]=std::to_wstring(option.initial);
    for(const auto& option:booleans) expected[option.key]=option.initial ? L"true" : L"false";
    expected.insert({{L"Appearance.fontFamily",L""},{L"Appearance.textAntialiasing",L"system"},{L"Appearance.middleEllipsis",L"gridOnly"},
        {L"Selection.filenameOnlySelection",L"gridOnly"},{L"Selection.selectionColor",L"accent"},
        {L"Columns.folderPlacement",L"explorerDefault"}});
    std::map<std::wstring,std::wstring> schema;
    std::wifstream source{std::filesystem::path(GPU_OVERLAY_SOURCE)};
    Check(source.is_open(),"selected settings source unavailable");
    bool settings=false; std::wstring line,group;
    const std::wregex category(LR"(^- ([A-Za-z][A-Za-z0-9]*):\s*$)"),
        option(LR"(^    - ([A-Za-z][A-Za-z0-9]*): (.*?)\s*$)"),
        rootOption(LR"(^- ([A-Za-z][A-Za-z0-9]*): (.*?)\s*$)");
    while(std::getline(source,line)) {
        if(line.find(L"// ==WindhawkModSettings==")!=std::wstring::npos) {settings=true; continue;}
        if(line.find(L"// ==/WindhawkModSettings==")!=std::wstring::npos) break;
        if(!settings) continue;
        std::wsmatch match;
        if(std::regex_match(line,match,rootOption)) {
            Check(schema.emplace(match[1].str(),match[2].str()).second,"duplicate root user option");
            group.clear();
        } else if(std::regex_match(line,match,category)) group=match[1];
        else if(std::regex_match(line,match,option)) {
            std::wstring value=match[2];
            if(value.size()>=2 && value.front()==L'"' && value.back()==L'"') value=value.substr(1,value.size()-2);
            Check(schema.emplace(group+L"."+match[1].str(),value).second,"duplicate user option");
        }
    }
    Check(schema==expected,"exposed options/defaults changed without updating regression coverage");
    auto values=schema;
    std::vector<std::function<void()>> restore;
    for(const auto& option:integers) {auto before=option.setting->load(); restore.push_back([=] {*option.setting=before;});}
    for(const auto& option:booleans) {auto before=option.setting->load(); restore.push_back([=] {*option.setting=before;});}
    std::atomic<bool>* diagnostics[]={&g_selectionLogging,&g_frameTimeLogging,
        &g_pipelineTracing,&g_uiTransitionLogging,&g_gpuMemoryLogging};
    for(auto* setting:diagnostics) {const bool before=setting->load(); restore.push_back([=] {*setting=before;});}
    const auto family=ConfiguredFontFamily(); const auto placement=g_folderPlacement.load();
    const auto middle=g_middleEllipsis.load(),filename=g_filenameOnly.load();
    const auto antialiasing=g_textAntialiasing.load();
    const auto rgb=g_selectionRgb.load(); const auto wheel=g_wheelLines.load();
    const bool accent=g_dynamicAccent.load();
    struct Restore {
        std::vector<std::function<void()>>& callbacks; std::function<void()> strings;
        ~Restore() {for(auto& callback:callbacks) callback(); strings(); SettingsFixture::values=nullptr;}
    } guard{restore,[&] {
        AcquireSRWLockExclusive(&g_fontFamilyLock); g_fontFamily=family; ReleaseSRWLockExclusive(&g_fontFamilyLock);
        g_folderPlacement=placement; g_middleEllipsis=middle; g_filenameOnly=filename;
        g_textAntialiasing=antialiasing; ++g_textRenderingRevision;
        g_selectionRgb=rgb; g_dynamicAccent=accent; g_wheelLines=wheel;
    }};
    SettingsFixture::reads.clear(); SettingsFixture::values=&values; LoadSettings();
    Check(SettingsFixture::reads.size()==schema.size(),"an exposed option is not loaded");
    for(const auto& [key,value]:schema) Check(SettingsFixture::reads[key]==1,"option read more than once or through the wrong key");
    for(const auto& option:integers) Check(option.setting->load()==option.initial,"integer default binding");
    for(const auto& option:booleans) Check(option.setting->load()==option.initial,"boolean default binding");
    for(auto* setting:diagnostics) Check(!setting->load(),"unified logging default left a diagnostic enabled");
    Check(ConfiguredFontFamily().empty() && g_dynamicAccent && g_middleEllipsis==ViewOption::GridOnly &&
        g_filenameOnly==ViewOption::GridOnly && g_folderPlacement==FolderPlacement::ExplorerDefault,"string default binding");
    const auto set=[&](const wchar_t* key,const std::wstring& value) {values[key]=value; LoadSettings();};
    for(const auto& [value,mode]:std::initializer_list<std::pair<const wchar_t*,TextAntialiasing>>{
        {L"system",TextAntialiasing::System},{L"smooth",TextAntialiasing::Smooth},
        {L"sharp",TextAntialiasing::System},{L"aliased",TextAntialiasing::Aliased},
        {L"SMOOTH",TextAntialiasing::Smooth},{L"unknown",TextAntialiasing::System}}) {
        set(L"Appearance.textAntialiasing",value);
        Check(g_textAntialiasing==mode,"text antialiasing choice/fallback binding");
    }
    values[L"Appearance.textAntialiasing"]=L"system"; LoadSettings();
    for(const auto& option:integers) {
        for(int value:{-6000,option.minimum,(option.minimum+option.maximum)/2,option.maximum,7000}) {
            set(option.key,std::to_wstring(value));
            Check(option.setting->load()==std::clamp(value,option.minimum,option.maximum),"integer range/clamping binding");
        }
        values[option.key]=schema.at(option.key);
    }
    for(const auto& option:booleans) {
        for(int value:{0,1,0,-3}) {
            set(option.key,std::to_wstring(value));
            Check(option.setting->load()==(value!=0),"boolean setting cannot be toggled in both directions");
            if(std::wstring_view(option.key)==L"enableLogging")
                for(auto* setting:diagnostics) Check(setting->load()==(value!=0),"unified logging did not toggle every diagnostic");
        }
        values[option.key]=schema.at(option.key);
    }
    for(const auto& [key,setting]:std::initializer_list<std::pair<const wchar_t*,std::atomic<ViewOption>*>>{
        {L"Appearance.middleEllipsis",&g_middleEllipsis},{L"Selection.filenameOnlySelection",&g_filenameOnly}}) {
        for(const auto& [value,expectedMode]:std::initializer_list<std::pair<const wchar_t*,ViewOption>>{
            {L"gridOnly",ViewOption::GridOnly},{L"alwaysOn",ViewOption::AlwaysOn},
            {L"alwaysOff",ViewOption::AlwaysOff},{L"unknown",ViewOption::GridOnly}}) {
            set(key,value); Check(setting->load()==expectedMode,"view option choice/fallback binding");
            ViewState state;
            for(bool grid:{false,true}) {
                state.gridView=grid;
                Check(ViewOptionEnabled(state,*setting)==(expectedMode==ViewOption::AlwaysOn ||
                    (expectedMode==ViewOption::GridOnly && grid)),"view option applied to the wrong layout");
            }
            Stop(state.background);
        }
        values[key]=schema.at(key);
    }
    for(const auto& [value,expectedMode]:std::initializer_list<std::pair<const wchar_t*,FolderPlacement>>{
        {L"explorerDefault",FolderPlacement::ExplorerDefault},{L"foldersTop",FolderPlacement::AlwaysTop},
        {L"foldersBottom",FolderPlacement::AlwaysBottom},{L"mixed",FolderPlacement::Mixed},
        {L"FOLDERSTOP",FolderPlacement::AlwaysTop},{L"unknown",FolderPlacement::ExplorerDefault}}) {
        set(L"Columns.folderPlacement",value); Check(g_folderPlacement==expectedMode,"folder placement choice/fallback binding");
    }
    set(L"Appearance.fontFamily",L" \tSegoe UI\r\n ");
    Check(ConfiguredFontFamily()==L"Segoe UI","font family trimming/binding");
    set(L"Appearance.fontFamily",L" \t\r\n"); Check(ConfiguredFontFamily().empty(),"blank font family fallback");
    for(const auto* value:{L"#12aBef",L"12aBef",L" \t#12aBef",L" #12aBef\r\n "}) {
        set(L"Selection.selectionColor",value);
        Check(!g_dynamicAccent && g_selectionRgb==RGB(0x12,0xAB,0xEF),"custom selection RGB parsing/binding");
    }
    for(const auto* value:{L"accent",L"ACCENT",L"",L"#xyzxyz",L"#12345",L"#1234567"}) {
        set(L"Selection.selectionColor",value); Check(g_dynamicAccent,"invalid/custom color failed to restore accent");
    }
    {
        StyleFixture::Session faults;
        set(L"Selection.selectionColor",L"#C08040");
        StyleFixture::accentStatus=E_FAIL;
        set(L"Selection.selectionColor",L"invalid");
        Check(g_dynamicAccent && g_selectionRgb==RGB(12,110,210),
            "failed accent lookup retained the previous custom color");
        StyleFixture::accentStatus=S_OK; StyleFixture::accent=0;
        set(L"Selection.selectionColor",L"accent");
        Check(g_selectionRgb==RGB(12,110,210),"transparent/uninitialized accent accepted");
        StyleFixture::accent=0xFFAACC11;
        set(L"Selection.selectionColor",L" accent\t");
        Check(g_dynamicAccent && g_selectionRgb==RGB(0xAA,0xCC,0x11),"valid DWM accent conversion/trim");
        StyleFixture::wheel=WHEEL_PAGESCROLL; LoadSettings();
        Check(g_wheelLines==WHEEL_PAGESCROLL,"page-scroll sentinel rejected");
        StyleFixture::wheel=0; LoadSettings(); Check(g_wheelLines==0,"disabled wheel scrolling rejected");
        StyleFixture::wheel=UINT_MAX-1; LoadSettings(); Check(g_wheelLines==3,"unusable wheel metric accepted");
        StyleFixture::failSystem=true; LoadSettings(); Check(g_wheelLines==3,"failed wheel lookup retained old value");
    }
    values=schema; LoadSettings(); TestOptionConsumers(set);
    Check(!SettingsFixture::strings,"settings string ownership leaked");
    std::cout<<"PASS: all "<<schema.size()<<" user options: schema completeness, exact defaults/runtime keys, both toggle directions, integer limits, all enum choices/fallbacks, font and color parsing, string ownership\n";
}

static void TestClippingPreflight() {
    const auto factory=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"clipping fonts");
    {
        PresentationFixture::Tab tab(L"clipping",3,false,false,false,1200,480);
        auto& state=tab.state;
        Check(MakeTextFormats(state),"clipping formats");
        ExplorerColumn author; author.key=PKEY_Author; author.title=std::wstring(32,L'W');
        ExplorerColumn name; name.key=PKEY_FileName; name.title=L"Name"; name.width=300;
        state.columns={author,name}; state.sort.explicitOrder=true; state.sort.key=PKEY_Author;
        for(auto& entry:state.entries) {
            entry.name=L"Short.txt"; entry.detailsReady=true; entry.columnEpoch=state.columnEpoch;
            entry.propertyEpoch=state.propertyEpoch; entry.cells={{PKEY_Author,L""}};
        }
        const auto measure=[&](IDWriteTextFormat* format,const std::wstring& value) {
            ComPtr<IDWriteTextLayout> layout; DWRITE_TEXT_METRICS metrics{};
            Check(SUCCEEDED(g_writeFactory->CreateTextLayout(value.c_str(),UINT32(value.size()),
                format,8192,200,&layout)) && SUCCEEDED(layout->GetMetrics(&metrics)),"independent clipping measurement");
            return metrics.widthIncludingTrailingWhitespace;
        };
        const float normal=measure(state.headerFormat.Get(),author.title),
            bold=measure(state.sortedHeaderFormat.Get(),author.title);
        Check(bold>normal+4,"sorted-header fixture requires distinct normal/bold metrics");
        for(bool rtl:{false,true}) {
            state.rtlLayout=rtl; MakeTextFormats(state);
            state.columns[0].width=int((normal+bold)*.5f)+Scale96(kColumnTextPadding96+kColumnFitSortInset96,ViewDpi(state))+2;
            UpdateColumns(state);
            Check(AutoRefitWouldImproveClipping(state),"sorted bold header clipping was measured with the unsorted font");
            state.columns[0].width=int(std::ceil(bold))+Scale96(kColumnTextPadding96+kColumnFitSortInset96,ViewDpi(state))+8;
            UpdateColumns(state);
            Check(!AutoRefitWouldImproveClipping(state),"readable header/body falsely reported clipping");
            for(auto& entry:state.entries) entry.cells={{PKEY_Author,std::wstring(100,L'W')}};
            const auto& text=state.entries.front().cells.front().text;
            ResolveTextLayout(state,text,50,state.rowPx); // Warm a previously ellipsized shape.
            Check(AutoRefitWouldImproveClipping(state),"warm ellipsized cell was incorrectly certified as readable");
            const float intrinsic=ColumnTextWidth(state,text,false);
            Check(std::abs(intrinsic-measure(state.textFormat.Get(),text))<.1f,
                "clipping preflight reused trimmed width instead of intrinsic text width");
            ResolveTextLayout(state,text,80,state.rowPx);
            Check(std::abs(ColumnTextWidth(state,text,false)-intrinsic)<.1f,"resize invalidated intrinsic text measurement");
            for(auto& entry:state.entries) entry.cells={{PKEY_Author,L""}};
        }
    }
    g_writeFactory=factory;
    std::cout<<"PASS: clipping preflight matches independent DirectWrite measurements for bold sorted headers, warm ellipsized cells, narrow/wide cache geometry and LTR/RTL\n";
}

// Real WM_SIZE -> owner continuation -> independent renderer -> receipt.
// The owner has no D2D target; tests that only call ScheduleResizeAutoRefit
// cannot detect either lost completion or a discarded resize request.
static void TestResizeAutoRefit() {
    const auto factory=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"resize fonts");
    const bool autoFit=g_autoRefitColumns.exchange(true);
    {
        PresentationFixture::Tab tab(L"resize-options",12);
        auto& state=tab.state;
        FocusFixture::Session focus(tab.frame,state.hwnd);
        state.columnManager.Attach(new CatalogColumnManager);
        state.lastColumnsProbe=1; state.nativeColumnsDirty=false;
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        const auto original=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(state.hwnd,GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(+[](HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)->LRESULT {
                if(msg==WM_SIZE || msg==WM_PAINT || msg==kMsgFrame || msg==kMsgColumnFitReady || msg==kMsgPresentationReady)
                    return OverlayProc(hwnd,msg,wp,lp);
                return DefWindowProcW(hwnd,msg,wp,lp);
            })));
        struct Restore {
            HWND hwnd,frame; WNDPROC original;
            ~Restore() {
                RemoveWindowSubclass(frame,ExplorerFrameSubclass,reinterpret_cast<UINT_PTR>(ExplorerFrameSubclass));
                UnregisterTabOverlay(frame,frame,hwnd);
                SetWindowLongPtrW(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original));
                SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
            }
        } restore{state.hwnd,tab.frame,original};
        RegisterTabOverlay(tab.frame,tab.frame,state.hwnd);
        SetWindowSubclass(tab.frame,ExplorerFrameSubclass,reinterpret_cast<UINT_PTR>(ExplorerFrameSubclass),0);
        tab.Start(); Check(EnsureTarget(state),"resize owner formats");
        tab.Await([](const auto& r) {return r.accepted;}); ConsumePresentation(state);
        RequestAnimatedColumnRefit(state,state.columns); Paint(state);
        tab.Await([](const auto& r) {return r.accepted && !r.columns && !r.columnPending;}); ConsumePresentation(state);
        const auto pump=[&] {
            MSG message{};
            while(PeekMessageW(&message,state.hwnd,0,0,PM_REMOVE)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
        };
        const auto settled=[&] {
            pump();
            return !state.autoRefitPending && !state.columnFitMessageQueued &&
                !state.columnTransitionActive && !state.columnPresentationPending;
        };
        const auto checkWidth=[&] {
            Check(std::abs(state.columnContentWidth-ListViewportWidth(state))<=1.f,
                "resize auto fit did not adapt columns to the actual viewport");
            Check(!state.target && state.presenter,"resize test bypassed independent presentation");
        };
        checkWidth();
        for(bool interactive:{false,true}) for(int width:{1100,1097,650,647}) {
            if(interactive) SendMessageW(tab.frame,WM_ENTERSIZEMOVE,0,0);
            Check(SetWindowPos(state.hwnd,nullptr,0,0,width,480,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"resize viewport");
            if(interactive) {
                pump();
                Check(state.autoRefitPending,"resize lost its pending fit during interactive sizing");
                SendMessageW(tab.frame,WM_EXITSIZEMOVE,0,0);
            }
            const bool complete=Eventually(settled);
            if(!complete) {
                const auto receipt=tab.Receipt();
                std::cout<<"RESIZE STALL width="<<width<<" interactive="<<interactive
                    <<" pending="<<state.autoRefitPending<<" queued="<<state.columnFitMessageQueued
                    <<" transition="<<state.columnTransitionActive<<" presentation="<<state.columnPresentationPending
                    <<" animating="<<state.presenter->animating.load()<<" sequence="<<state.presentationSequence
                    <<" receipt="<<(receipt ? receipt->sequence:0)<<" columns="<<(receipt && receipt->columns)
                    <<" column-pending="<<(receipt && receipt->columnPending)<<" accepted="<<(receipt && receipt->accepted)
                    <<" frame="<<(receipt ? receipt->frames:0)<<" composition="<<g_compositionCompleted.load()<<std::endl;
            }
            Check(complete,"resize fit never completed through presentation acknowledgements");
            checkWidth();
        }
        // Several ellipsized columns after a small change to a wide window:
        // test the preflight independently as well as the WM_SIZE trigger.
        ExplorerColumn name=state.columns.front(),modified=name,type=name;
        name.width=140; modified.width=140; type.width=140;
        modified.key=PKEY_DateModified; modified.title=L"Date modified";
        type.key=PKEY_ItemTypeText; type.title=L"Item type";
        state.columns={name,modified,type};
        for(auto& entry:state.entries) {
            entry.name=L"A very long filename that cannot fit in its old narrow column.txt";
            entry.date=L"Wednesday, 24 October 2026 12:34:56";
            entry.type=L"An unusually descriptive file type label";
            entry.detailsReady=true; entry.columnEpoch=state.columnEpoch; entry.propertyEpoch=state.propertyEpoch;
        }
        state.presentationEntries=std::make_shared<const std::vector<Entry>>(state.entries);
        UpdateColumns(state);
        Check(AutoRefitWouldImproveClipping(state),"clipping preflight certified several ellipsized columns as readable");
        state.lastAutoFitViewport={647,480};
        SetWindowPos(state.hwnd,nullptr,0,0,645,480,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        Check(Eventually(settled),"small resize with ellipsized columns never completed"); checkWidth();
        // No-op WM_SIZE echoes cannot restart an already settled transition.
        SendMessageW(state.hwnd,WM_SIZE,SIZE_RESTORED,MAKELPARAM(645,480)); pump();
        Check(!state.autoRefitPending && !state.columnTransitionActive,"unchanged size restarted auto fit");
        state.columns={name}; state.columns.front().width=600; UpdateColumns(state);
        for(auto& entry:state.entries) entry.name=L"Short name";
        g_autoRefitColumns=false;
        const int before=state.columns.front().width;
        SetWindowPos(state.hwnd,nullptr,0,0,1000,480,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE); pump();
        Check(!state.autoRefitPending && state.columns.front().width==before,"disabled resize auto fit changed widths");
        // Ordinary navigation still preserves a readable user layout.
        g_autoRefitColumns=true;
        Check(!AutoRefitWouldImproveClipping(state),"readable resize fixture unexpectedly clips");
        ArmAutoRefit(state); pump();
        Check(state.columns.front().width==before && !state.autoRefitPending,"navigation refit changed readable widths");
    }
    ShutDownFrames(); g_frameStopping=false;
    g_autoRefitColumns=autoFit; g_writeFactory=factory;
    std::cout<<"PASS: enabled/disabled auto-fit across actual growing/shrinking programmatic and interactive WM_SIZE, independent render completion and readable navigation preservation\n";
}
