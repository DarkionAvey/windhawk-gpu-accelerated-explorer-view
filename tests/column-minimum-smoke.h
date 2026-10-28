// Exercise the production fitter and scrollbar geometry with real DirectWrite
// formats at different densities. A private owner window supplies real geometry
// and horizontal-bar admission; no Explorer providers or user files are used.
namespace ColumnMinimumFixture {
float Measure(IDWriteTextFormat* format,const std::wstring& text) {
    ComPtr<IDWriteTextLayout> layout; DWRITE_TEXT_METRICS metrics{};
    Check(SUCCEEDED(g_writeFactory->CreateTextLayout(text.c_str(),UINT32(text.size()),
        format,8192,2000,&layout)) && SUCCEEDED(layout->GetMetrics(&metrics)),
        "column minimum independent font measurement");
    return metrics.widthIncludingTrailingWhitespace;
}
void Readable(ViewState& state) {
    const int dpi=ViewDpi(state);
    for(const auto& column:state.columns) {
        const bool name=SameKey(column.key,PKEY_FileName);
        const float leading=name ? NameTextOrigin(state)-NameColumnStart(state) : Scale96(16,dpi);
        const float room=column.end-column.start-leading-(name ? 8.f : 6.f);
        Check(column.width>=Scale96(name ? 180 : 112,dpi),"auto fit collapsed a column below its readable DPI floor");
        Check(room+.1f>=Measure(state.textFormat.Get(),name ? L"000000000000" : L"0000000000"),
            "auto fit left too few readable characters after icon/text insets");
        const bool sorted=state.sort.explicitOrder && SameKey(column.key,state.sort.key);
        const float caption=Measure(sorted ? state.sortedHeaderFormat.Get() : state.headerFormat.Get(),column.title);
        if(column.title.size()<24) Check(column.end-column.start-leading-Scale96(sorted ? 30 : 16,dpi)+.1f>=caption,
            "auto fit unnecessarily ellipsized an ordinary column header");
    }
}
}

static void TestColumnMinimumWidths() {
    const auto factory=g_writeFactory;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"column minimum fonts");
    const bool scrollbar=g_scrollbar.exchange(true);
    const int font=g_fontPercent.exchange(52),edge=g_edgeMargin96.exchange(10);
    for(int dpi:{96,144,192}) for(bool rtl:{false,true}) for(int fontPercent:{20,52,80}) {
        StyleFixture::Session metrics; StyleFixture::dpi=UINT(dpi);
        StyleFixture::themeFont.lfHeight=-Scale96(12,dpi); StyleFixture::themeFont.lfWeight=FW_NORMAL;
        wcscpy_s(StyleFixture::themeFont.lfFaceName,L"Segoe UI");
        ViewState state;
        state.hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"column minimum owner",WS_POPUP,
            -32000,-32000,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state.hwnd!=nullptr,"column minimum owner window");
        struct Drain {ViewState& state; ~Drain() {DestroyWindow(state.hwnd); Stop(state.background);}} drain{state};
        state.frameHwnd=state.activeTabHost=state.hwnd; state.rtlLayout=rtl;
        state.visible=state.inScope=state.loaded=state.detailsViewActive=state.columnsView=true;
        g_fontPercent=fontPercent;
        const auto resize=[&](int width) {
            Check(SetWindowPos(state.hwnd,nullptr,0,0,width,Scale96(400,dpi),
                SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"column minimum private resize");
        };
        resize(Scale96(560,dpi));
        state.total=200; state.entries.resize(state.total);
        for(auto& entry:state.entries) {
            entry.name=L"A reasonably descriptive filename.cpp";
            entry.date=L"24/10/2026 12:34"; entry.size=L"123,456 KB"; entry.type=L"C++ source file";
            entry.cells={{PKEY_Author,L"Original location example"},{PKEY_DateCreated,L"23/10/2026 20:45"}};
            entry.detailsReady=true; entry.columnEpoch=state.columnEpoch; entry.propertyEpoch=state.propertyEpoch;
        }
        state.presentationEntries=std::make_shared<const std::vector<Entry>>(state.entries);
        const auto column=[](PROPERTYKEY key,const wchar_t* title) {
            ExplorerColumn result; result.key=key; result.title=title; result.width=240; result.defaultWidth=240; return result;
        };
        state.columns={column(PKEY_FileName,L"Name"),column(PKEY_Author,L"Original location"),
            column(PKEY_DateCreated,L"Date deleted"),column(PKEY_Size,L"Size"),
            column(PKEY_ItemTypeText,L"Item type"),column(PKEY_DateModified,L"Date modified")};
        state.sort.explicitOrder=true; state.sort.key=PKEY_DateCreated;
        UpdateMetrics(state); Check(MakeTextFormats(state),"column minimum text formats");
        const auto fit=[&] {FitColumnsToVisibleRows(state); UpdateColumns(state); ColumnMinimumFixture::Readable(state);};
        fit();
        Check(HasVerticalScrollbar(state),"column minimum fixture lacks vertical scrollbar pressure");
        const auto bar=HorizontalBar(state);
        Check(bar.usable && bar.scrollRange>Scale96(100,dpi),"readability overflow did not expose horizontal scrolling");
        SetHorizontalScroll(state,bar.scrollRange);
        Check(std::abs(state.columns.back().end-state.horizontalScroll-ListViewportWidth(state))<1.f,
            "horizontal scrolling cannot reach the trailing metadata column");
        SetHorizontalScroll(state,0);
        const int narrowName=state.columns.front().width,narrowMetadata=state.columns[3].width;
        // Empty folders use the same floors, including font/icon allowance.
        state.total=0; fit();
        // A few pathological headers must not impose a multi-thousand-pixel
        // minimum. They may still ellipsize while ordinary labels stay whole.
        state.columns[1].title=std::wstring(300,L'W'); fit();
        Check(state.columns[1].width<Scale96(500,dpi),"unbounded header became an automatic minimum");
        state.columns[1].title=L"Original location"; fit();
        const int floorSum=int(state.columnContentWidth);
        // Test exact-fit rounding and the vertical-scrollbar clearance sliver.
        // The latter used to quietly shrink the last column after fitting.
        for(int rows:{0,200}) {
            state.total=rows;
            for(int delta:{-2,0,2}) {
                resize(floorSum+2*Scale96(10,dpi)+(rows ? VerticalScrollbarClearance(dpi) : 0)+delta);
                fit();
                if(delta<0) Check(state.columnContentWidth>ListViewportWidth(state)+1.f && HorizontalBar(state).usable,
                    "scrollbar clearance silently compressed a readable minimum");
                else Check(std::abs(state.columnContentWidth-ListViewportWidth(state))<=1.f && !HorizontalHeight(state),
                    "rounding created unnecessary horizontal overflow when readable columns fit");
            }
        }
        resize(Scale96(1800,dpi)); fit();
        Check(!HorizontalHeight(state) && std::abs(state.columnContentWidth-ListViewportWidth(state))<=1.f,
            "wide resize retained overflow instead of distributing available detail space");
        // Deliberate manual sizing continues to follow the existing divider
        // constraints; the readability rule belongs to automatic fitting.
        state.dragDivider=0; state.dragStartX=0; state.dragStartPos=Scale96(55,dpi);
        DragDivider(state,LogicalX(state,0));
        Check(state.columns.front().width==Scale96(55,dpi),"auto-fit readability floor overrode a deliberate divider drag");
        if(!rtl && dpi==96) std::cout<<"COLUMN MINIMUM font="<<fontPercent<<"% name="<<narrowName
            <<" metadata="<<narrowMetadata<<" narrow horizontal range="<<bar.scrollRange<<" px\n";
    }
    g_edgeMargin96=edge; g_fontPercent=font; g_scrollbar=scrollbar; g_writeFactory=factory;
    std::cout<<"PASS: readable fitted columns at 96/144/192 DPI, 20/52/80% fonts and LTR/RTL; headers/icons, empty/populated rows, bounded captions, scrollbar clearance, rounding, narrow/wide resizing and manual dividers\n";
}
