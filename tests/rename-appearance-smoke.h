// Native EDIT paint colors, font metrics and real window resizing on private
// fixtures. No desktop input, user folders, clipboard or focus changes.
namespace RenameAppearanceFixture {
LRESULT CALLBACK Resize(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (msg == WM_SIZE || msg == WM_MOVE) return OverlayProc(hwnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, Resize, id);
    return DefSubclassProc(hwnd, msg, wp, lp);
}
void Bounds(ViewState& state) {
    RECT actual{}; GetWindowRect(state.rename.editor, &actual);
    MapWindowPoints(nullptr, state.frameHwnd, reinterpret_cast<POINT*>(&actual), 2);
    const RECT viewport = RenameViewport(state);
    Check(IsWindowVisible(state.rename.editor) && actual.left >= viewport.left &&
        actual.right <= viewport.right && actual.top >= viewport.top && actual.bottom <= viewport.bottom,
        "rename EDIT escaped the item viewport");
}
void TextAlignment(ViewState& state) {
    const HWND editor=state.rename.editor;
    HDC dc = GetDC(editor);
    const auto font = reinterpret_cast<HFONT>(SendMessageW(editor, WM_GETFONT, 0, 0));
    const auto old = SelectObject(dc, font);
    TEXTMETRICW metrics{}; Check(GetTextMetricsW(dc, &metrics), "rename actual font metrics");
    SelectObject(dc, old); ReleaseDC(editor, dc);
    RECT window{}, client{}; GetWindowRect(editor, &window); GetClientRect(editor, &client);
    if (GetWindowLongPtrW(editor, GWL_STYLE) & ES_MULTILINE) {
        RECT format{}; SendMessageW(editor, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
        Check((GetWindowLongPtrW(editor,GWL_STYLE)&(ES_CENTER|ES_RIGHT))==ES_CENTER,
            "grid rename text did not use native centered alignment");
        MapWindowPoints(editor,state.hwnd,reinterpret_cast<POINT*>(&format),2);
        const auto geometry=LayoutItem(state,{state.headerPx+DocumentRowTop(state,state.rename.index)-state.scrollNow,
            DisplayRowHeight(state,state.rename.index),1.f,DocumentRowLeft(state,state.rename.index)});
        RECT overlay{}; ViewClientRect(state,&overlay);
        const float left=state.rtlLayout ? overlay.right-geometry.text.right : geometry.text.left;
        const auto viewport=RenameViewport(state,false);
        // Tiny resized viewports can clip the text band; check exact label
        // alignment whenever it fits, and containment in Bounds otherwise.
        if(left>=viewport.left && left+geometry.text.right-geometry.text.left<=viewport.right &&
            geometry.text.top>=viewport.top && geometry.text.bottom<=viewport.bottom) {
            Check(std::abs(format.left-left)<=1.f &&
                std::abs(format.right-format.left-(geometry.text.right-geometry.text.left))<=1.f &&
                std::abs(format.top-geometry.text.top)<=1.f,
                "grid rename formatting did not match the label origin/wrapping width");
        }
    } else {
        MapWindowPoints(editor, nullptr, reinterpret_cast<POINT*>(&client), 2);
        const LONG available=std::max(0L,window.bottom-window.top-
            2*GetSystemMetricsForDpi(SM_CYBORDER,StyleFixture::dpi));
        Check(std::abs((client.top-window.top) - (window.bottom-client.bottom)) <= 1 &&
            std::abs((client.bottom-client.top)-std::min(metrics.tmHeight,available)) <= 1,
            "single-line rename text was not centered using the selected HFONT");
    }
}
struct PaintColors {
    HWND editor;
    COLORREF text=CLR_INVALID, background=CLR_INVALID, brush=CLR_INVALID;
    unsigned calls=0;
};
LRESULT CALLBACK ObserveColors(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto& colors=*reinterpret_cast<PaintColors*>(data);
    const LRESULT result=DefSubclassProc(hwnd,msg,wp,lp);
    if((msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORSTATIC) && reinterpret_cast<HWND>(lp)==colors.editor) {
        LOGBRUSH brush{};
        Check(GetObjectW(reinterpret_cast<HBRUSH>(result),sizeof(brush),&brush),"native EDIT paint brush");
        colors.text=GetTextColor(reinterpret_cast<HDC>(wp));
        colors.background=GetBkColor(reinterpret_cast<HDC>(wp)); colors.brush=brush.lbColor; ++colors.calls;
    }
    return result;
}
void Palette(ViewState& state) {
    // Let the real EDIT request colors while USER paints it. PrintWindow is
    // not supported for these offscreen EDITs; no paint messages are fabricated.
    PaintColors colors{state.rename.editor};
    Check(SetWindowSubclass(state.frameHwnd,ObserveColors,1,reinterpret_cast<DWORD_PTR>(&colors)),"rename paint observer");
    RedrawWindow(colors.editor,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW|RDW_FRAME);
    RemoveWindowSubclass(state.frameHwnd,ObserveColors,1);
    Check(colors.calls && colors.text==state.nativeEditForeground &&
        colors.background==state.nativeEditBackground && colors.brush==state.nativeEditBackground,
        "native EDIT paint did not use the cached theme background/text/brush");
}
}

static void TestRenameAppearance(const std::wstring& directory) {
    StyleFixture::Session styles;
    const auto factory=g_writeFactory;
    const auto filenameOnly=g_filenameOnly.exchange(ViewOption::GridOnly);
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"rename appearance fonts");
    for(int dpi:{96,144,192}) for(bool rtl:{false,true})
        for(UINT mode:{FVM_DETAILS,FVM_LIST,FVM_SMALLICON,FVM_ICON,FVM_THUMBNAIL}) for(bool grouped:{false,true}) {
        const bool grid=mode==FVM_ICON || mode==FVM_SMALLICON || mode==FVM_THUMBNAIL, details=mode==FVM_DETAILS;
        StyleFixture::dpi=dpi;
        PresentationFixture::Tab tab(L"rename appearance",80,rtl,grid,grouped,Scale96(330,dpi),Scale96(210,dpi));
        auto& state=tab.state;
        ApplyViewLayout(state,{mode,mode==FVM_THUMBNAIL ? 192 : 48},false);
        if(rtl) SetWindowLongPtrW(tab.frame,GWL_EXSTYLE,GetWindowLongPtrW(tab.frame,GWL_EXSTYLE)|WS_EX_LAYOUTRTL);
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        Check(SetWindowSubclass(state.hwnd,RenameAppearanceFixture::Resize,1,0),"rename resize delivery");
        FocusFixture::Session focus(tab.frame,state.hwnd);
        const auto path=std::filesystem::path(directory)/(L"rename-appearance-"+std::to_wstring(dpi)+
            std::to_wstring(rtl)+std::to_wstring(mode)+std::to_wstring(grouped));
        state.folderPath=path.wstring();
        auto& entry=state.entries.back();
        entry.name=grouped ? L"a longer original filename that wraps across several lines" : L"renaming";
        entry.path=(path/entry.name).wstring(); std::filesystem::create_directories(entry.path);
        auto* view=new SelectionFolderView;
        ComPtr<IShellItem> item;
        Check(SUCCEEDED(SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&item))) &&
            SUCCEEDED(item->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&state.folder))),"rename appearance private Shell folder");
        view->folder=state.folder; view->state=&state; view->reenterCapture=false; state.folderView.Attach(view);
        state.selected=state.anchor=state.total-1;
        state.selectedFlags.assign(state.total,0); state.selectedFlags[size_t(state.selected)]=1;
        if(details) {
            ExplorerColumn type; type.key=PKEY_ItemTypeText; type.width=Scale96(350,dpi);
            ExplorerColumn name; name.key=PKEY_FileName; name.width=Scale96(450,dpi);
            ExplorerColumn size; size.key=PKEY_Size; size.width=Scale96(500,dpi);
            state.columns={type,name,size}; UpdateColumns(state);
        }
        RefreshNativePalette(state,true);
        for(bool trailing:{false,true}) {
            g_filenameOnly=trailing ? ViewOption::AlwaysOff : ViewOption::GridOnly;
            FocusFixture::focus=state.hwnd;
            if(details) SetHorizontalScroll(state,trailing ? state.columnContentWidth : 0.f);
            const float before=state.horizontalScroll;
            SetScrollImmediate(state,0.f);
            BeginRename(state,state.selected);
            Check(state.rename.editor && state.scrollNow>0.f && !state.animationActive &&
                !state.horizontalAnimating,"rename did not immediately reveal the offscreen row");
            if(details) Check(trailing ? state.horizontalScroll<before : state.horizontalScroll>before,
                "rename did not reveal the moved Name column from both horizontal edges");
            RenameAppearanceFixture::Bounds(state);
            RenameAppearanceFixture::TextAlignment(state);
            if(grid && mode!=FVM_SMALLICON && !trailing) {
                // The owner can open rename before acquiring any DirectWrite
                // resources. Only now build them to inspect its old label.
                Check(MakeTextFormats(state),"grid rename selection measurement");
                RECT actual{}, overlay{}; GetWindowRect(state.rename.editor,&actual); ViewClientRect(state,&overlay);
                state.paintWidth=static_cast<float>(overlay.right);
                const float top=state.headerPx+DocumentRowTop(state,state.selected)-state.scrollNow;
                const auto selection=SelectionRect(state,state.selected,top,DisplayRowHeight(state,state.selected),
                    0.f,0.f,ListEndX(state));
                MapWindowPoints(nullptr,state.hwnd,reinterpret_cast<POINT*>(&actual),2);
                const float left=state.rtlLayout ? overlay.right-selection.right : selection.left;
                const float right=state.rtlLayout ? overlay.right-selection.left : selection.right;
                const bool covered=actual.left<=left && actual.right>=right && actual.top<=selection.top && actual.bottom>=selection.bottom;
                if(!covered) std::cout<<"RENAME coverage dpi="<<dpi<<" rtl="<<rtl<<" mode="<<mode<<" grouped="<<grouped
                    <<" edit="<<actual.left<<","<<actual.top<<","<<actual.right<<","<<actual.bottom
                    <<" selection="<<left<<","<<selection.top<<","<<right<<","<<selection.bottom<<std::endl;
                Check(covered,
                    "grid rename exposed the original filename selection outside the EDIT");
            }
            Check(SelectionFeedbackVisible(state) && !OverlayHasInputFocus(state),
                "rename lost selection feedback or granted list input");
            const auto model=CapturePresentationModel(state);
            Check(model->selectionFeedback && !model->inputFocus && model->renameIndex==state.selected,
                "rename selection/editor state did not cross the presentation boundary");
            if(!trailing) {
                TouchInputTest::RenderedTouch rendered;
                FocusFixture::focus=tab.frame;
                const auto plain=rendered.Capture(state);
                FocusFixture::focus=state.rename.editor;
                const auto selected=rendered.Capture(state);
                unsigned highlights=0;
                for(size_t p=3;p<plain.size();p+=4) highlights+=selected[p]>plain[p]+10;
                Check(highlights>20,"renamed item selection background was absent from actual GPU pixels");
            }
            for(const wchar_t* face:{L"Arial",L"Consolas"}) {
                HFONT font=CreateFontW(-Scale96(13,dpi),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                    DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,face);
                Check(font!=nullptr,"rename metric font");
                SendMessageW(state.rename.editor,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
                for(const wchar_t* text:{L"短い名前.ext",L"a longer filename that wraps across several lines.ext"}) {
                    SetWindowTextW(state.rename.editor,text); UpdateWindow(state.rename.editor);
                    RenameAppearanceFixture::Bounds(state); RenameAppearanceFixture::TextAlignment(state);
                    RECT format{}, after{}; SendMessageW(state.rename.editor,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
                    for(int n=0;n<3;++n) RepositionRenameEdit(state);
                    SendMessageW(state.rename.editor,EM_GETRECT,0,reinterpret_cast<LPARAM>(&after));
                    Check(EqualRect(&format,&after),"rename centering repeatedly inset the native formatting rectangle");
                }
                SendMessageW(state.rename.editor,WM_SETFONT,reinterpret_cast<WPARAM>(state.rename.font),TRUE);
                DeleteObject(font);
            }
            for(bool light:{false,true}) {
                StyleFixture::appBg=light ? ABI::Windows::UI::Color{255,245,245,245} : ABI::Windows::UI::Color{255,24,24,24};
                StyleFixture::appFg=light ? ABI::Windows::UI::Color{255,16,16,16} : ABI::Windows::UI::Color{255,240,240,240};
                RefreshNativePalette(state,true); RenameAppearanceFixture::Palette(state);
            }
            StyleFixture::highContrast=true;
            RefreshNativePalette(state,true);
            Check(state.nativeEditBackground==StyleFixture::window && state.nativeEditForeground==StyleFixture::text,
                "rename did not honor exact high contrast colors");
            RenameAppearanceFixture::Palette(state); StyleFixture::highContrast=false;
            // USER supplies WM_SIZE/WM_MOVE from the actual geometry change.
            SetWindowPos(state.hwnd,nullptr,Scale96(25,dpi),Scale96(15,dpi),Scale96(200,dpi),Scale96(130,dpi),
                SWP_NOZORDER|SWP_NOACTIVATE);
            RenameAppearanceFixture::Bounds(state); RenameAppearanceFixture::TextAlignment(state);
            CancelRename(state);
            Check(!state.rename.background && !state.rename.parentSubclassInstalled,"rename theme resources leaked");
            SetWindowPos(state.hwnd,nullptr,0,0,Scale96(330,dpi),Scale96(210,dpi),SWP_NOZORDER|SWP_NOACTIVATE);
        }
        RemoveWindowSubclass(state.hwnd,RenameAppearanceFixture::Resize,1);
    }
    g_filenameOnly=filenameOnly; g_writeFactory=factory;
    std::cout<<"PASS: native rename colors, HFONT metrics, centered grid label origins/wrapping and selection coverage, GPU selection pixels, two-axis reveal and real resize containment in grouped/ungrouped LTR/RTL Details/List/small/large icons at 96/144/192 DPI\n";
}
