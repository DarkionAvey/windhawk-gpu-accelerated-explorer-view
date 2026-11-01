// Native EDIT paint colors, font metrics and real window resizing on private
// fixtures. No desktop input, user folders, clipboard or focus changes.
namespace RenameAppearanceFixture {
LRESULT CALLBACK Resize(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (msg == WM_SIZE || msg == WM_MOVE) return OverlayProc(hwnd, msg, wp, lp);
    if (msg == kMsgPresentationReady || (msg == WM_PAINT && State(hwnd) && State(hwnd)->presenter))
        return OverlayProc(hwnd, msg, wp, lp);
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
        const auto geometry=RenameItemGeometry(state,state.rename.index);
        RECT format{}; SendMessageW(editor, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
        const LONG_PTR style=GetWindowLongPtrW(editor,GWL_STYLE),ex=GetWindowLongPtrW(editor,GWL_EXSTYLE);
        // Windows translates inherited mirrored EDIT alignment to WS_EX_RIGHT.
        const LONG_PTR alignment=geometry.wrapped ? ES_CENTER : state.rtlLayout ? ES_RIGHT : ES_LEFT;
        const LONG_PTR actual=(style&ES_CENTER) ? ES_CENTER : ((style&ES_RIGHT) || (ex&WS_EX_RIGHT)) ? ES_RIGHT : ES_LEFT;
        if(actual!=alignment)
            std::cout<<"RENAME alignment dpi="<<StyleFixture::dpi<<" rtl="<<state.rtlLayout<<" wrapped="<<geometry.wrapped
                <<" style="<<GetWindowLongPtrW(editor,GWL_STYLE)<<" expected="<<alignment<<std::endl;
        Check(actual==alignment,
            "rename text did not use the label's native alignment");
        // Native EDIT can retain a minimum formatting height when the entire
        // viewport is shorter than one glyph. Window clipping still applies.
        const bool vertical= format.top>=0 && (format.bottom<=client.bottom || client.bottom<metrics.tmHeight);
        if(!(std::abs(format.left-(client.right-format.right))<=1 && vertical))
            std::cout<<"RENAME padding dpi="<<StyleFixture::dpi<<" rtl="<<state.rtlLayout<<" wrapped="<<geometry.wrapped
                <<" client="<<client.right<<","<<client.bottom<<" format="<<format.left<<","<<format.top<<","<<format.right<<","<<format.bottom
                <<" lines="<<SendMessageW(editor,EM_GETLINECOUNT,0,0)<<" line="<<RenameLineHeight(editor)<<std::endl;
        Check(std::abs(format.left-(client.right-format.right))<=1 && vertical,
            "expanded rename text lost symmetric native padding");
        MapWindowPoints(editor,state.hwnd,reinterpret_cast<POINT*>(&format),2);
        RECT overlay{}; ViewClientRect(state,&overlay);
        const float left=state.rtlLayout ? overlay.right-geometry.text.right : geometry.text.left;
        const auto viewport=RenameViewport(state,false);
        // Tiny resized viewports can clip the text band; check exact label
        // alignment whenever it fits, and containment in Bounds otherwise.
        if(geometry.wrapped && left>=viewport.left && left+geometry.text.right-geometry.text.left<=viewport.right &&
            geometry.text.top>=viewport.top && geometry.text.bottom<=viewport.bottom) {
            Check(std::abs(format.left-left)<=1.f &&
                std::abs(format.right-format.left-(geometry.text.right-geometry.text.left))<=1.f &&
                format.top>=0,
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
void Prefix(ViewState& state) {
    const HWND edit=state.rename.editor;
    RECT format{}; SendMessageW(edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
    const LRESULT position=SendMessageW(edit,EM_POSFROMCHAR,0,0);
    const POINT first{static_cast<short>(LOWORD(position)),static_cast<short>(HIWORD(position))};
    // Single-line EDIT can report first-visible=0 while its actual first
    // character has a negative X. Check the native character coordinates.
    Check(position!=-1 && first.x>=format.left && first.x<=format.right &&
        first.y>=format.top && first.y<format.top+RenameLineHeight(edit),
        "initial narrow rename field clipped the filename prefix");
    DWORD begin=0,end=0;
    SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&begin),reinterpret_cast<LPARAM>(&end));
    const auto& entry=state.entries[size_t(state.rename.index)];
    size_t selected=entry.name.size();
    const auto dot=entry.name.find_last_of(L'.');
    if(!entry.folder && dot!=std::wstring::npos && dot>0) selected=dot;
    Check(begin==0 && end==selected,"prefix reveal changed the native basename selection");
}
void AllLines(ViewState& state) {
    if(!(GetWindowLongPtrW(state.rename.editor,GWL_STYLE)&ES_MULTILINE)) return;
    RECT format{}; SendMessageW(state.rename.editor,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
    const LONG lines=LONG(SendMessageW(state.rename.editor,EM_GETLINECOUNT,0,0));
    const LONG height=lines*RenameLineHeight(state.rename.editor);
    const auto viewport=RenameViewport(state,false);
    const auto padding=RenameTextPadding(state);
    if(height<=viewport.bottom-viewport.top-padding.cy*2)
        Check(format.bottom-format.top>=height && SendMessageW(state.rename.editor,EM_GETFIRSTVISIBLELINE,0,0)==0,
            "expanded rename EDIT clipped native wrapped lines or retained a stale text scroll");
}
struct PaintLayoutProbe { unsigned positions=0,formats=0,fonts=0; };
LRESULT CALLBACK ObservePaintLayout(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
    auto& probe=*reinterpret_cast<PaintLayoutProbe*>(data);
    if(msg==WM_WINDOWPOSCHANGING) ++probe.positions;
    if(msg==EM_SETRECT || msg==EM_SETRECTNP) ++probe.formats;
    if(msg==WM_SETFONT) ++probe.fonts;
    if(msg==WM_NCDESTROY) RemoveWindowSubclass(hwnd,ObservePaintLayout,id);
    return DefSubclassProc(hwnd,msg,wp,lp);
}
void SelectionPaint(ViewState& state) {
    const HWND edit=state.rename.editor;
    std::wstring saved(size_t(GetWindowTextLengthW(edit))+1,L'\0'); GetWindowTextW(edit,saved.data(),int(saved.size()));
    const std::wstring longName=std::wstring(250,L'W')+L".txt";
    SetWindowTextW(edit,longName.c_str()); UpdateWindow(edit); AllLines(state);
    RECT before{},after{}; GetWindowRect(edit,&before);
    PaintLayoutProbe probe;
    Check(SetWindowSubclass(edit,ObservePaintLayout,1,reinterpret_cast<DWORD_PTR>(&probe)),"rename selection paint observer");
    for(int n=2;n<36;++n) {
        // Exercise the actual EDIT selection/scroll APIs and USER paint path;
        // do not fabricate mouse messages or move the user's pointer/focus.
        SendMessageW(edit,EM_SETSEL,1,n); SendMessageW(edit,EM_SCROLLCARET,0,0);
        RedrawWindow(edit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
    }
    // Owner geometry updates can occur during a real drag too. An expanded
    // field must not shrink/re-expand just to repeat its native measurement.
    for(int n=0;n<3;++n) RepositionRenameEdit(state);
    GetWindowRect(edit,&after); RemoveWindowSubclass(edit,ObservePaintLayout,1);
    Check(EqualRect(&before,&after) && !probe.positions && !probe.formats && !probe.fonts,
        "native selection painting resized/reformatted the rename field");
    SetWindowTextW(edit,saved.c_str()); UpdateWindow(edit);
}
void FontQuality(ViewState& state) {
    const auto previous=g_textAntialiasing.load();
    for(auto mode:{TextAntialiasing::Aliased,TextAntialiasing::Smooth,TextAntialiasing::System}) {
        g_textAntialiasing=mode; ++g_textRenderingRevision;
        RepositionRenameEdit(state);
        LOGFONTW font{}; GetObjectW(reinterpret_cast<HFONT>(SendMessageW(state.rename.editor,WM_GETFONT,0,0)),sizeof(font),&font);
        BOOL smoothing=TRUE; UINT type=0;
        BYTE expected=mode==TextAntialiasing::Aliased ? NONANTIALIASED_QUALITY : ANTIALIASED_QUALITY;
        if(mode==TextAntialiasing::System) {
            expected=DEFAULT_QUALITY;
            if(SystemParametersInfoW(SPI_GETFONTSMOOTHING,0,&smoothing,0)) {
                if(!smoothing) expected=NONANTIALIASED_QUALITY;
                else if(SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE,0,&type,0))
                    expected=type==FE_FONTSMOOTHINGCLEARTYPE ? CLEARTYPE_QUALITY : ANTIALIASED_QUALITY;
            }
        }
        Check(font.lfQuality==expected,"native rename font did not follow system/mod smoothing policy");
    }
    g_textAntialiasing=previous; ++g_textRenderingRevision; RepositionRenameEdit(state);
}
void Zoom(PresentationFixture::Tab& tab) {
    auto& state=tab.state;
    const bool smooth=g_smooth.exchange(true);
    const HWND editor=state.rename.editor;
    const int initial=state.rowHeight96[state.gridView] ? state.rowHeight96[state.gridView] : g_row96.load();
    std::wstring text(size_t(GetWindowTextLengthW(editor))+1,L'\0'); GetWindowTextW(editor,text.data(),int(text.size()));
    SendMessageW(editor,EM_SETSEL,1,5);
    const auto preserved=[&] {
        DWORD first=0,last=0; SendMessageW(editor,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
        std::wstring after(text.size(),L'\0'); GetWindowTextW(editor,after.data(),int(after.size()));
        LOGFONTW font{}; GetObjectW(reinterpret_cast<HFONT>(SendMessageW(editor,WM_GETFONT,0,0)),sizeof(font),&font);
        Check(state.rename.editor==editor && text==after && first==1 && last==5 &&
            font.lfHeight==-LONG(std::lround(state.fontPx)),"zoom changed rename text/selection or restored an obsolete font");
        Bounds(state); AllLines(state);
    };
    ApplyRowHeight(state,initial+12);
    Check(state.rename.zoomHidden && !IsWindowVisible(editor) && !CapturePresentationModel(state)->renameEditor,
        "wheel zoom left its native rename editor visible or suppressed its GPU label");
    RepositionRenameEdit(state); Check(!IsWindowVisible(editor),"geometry update exposed an editor during zoom");
    AdvanceRowHeight(state,true); preserved();
    // Touchpad release into inertia is not READY. Shared gesture completion
    // must keep the editor hidden even after its last zoom frame finishes.
    ApplyManipulationStatus(state,1); ApplyManipulationPinch(state,std::log(1.1f));
    AdvanceRowHeight(state,true); Check(!IsWindowVisible(editor),"held pinch restored the rename editor");
    ApplyManipulationStatus(state,2); Check(!IsWindowVisible(editor),"pinch inertia restored the rename editor before READY");
    ApplyManipulationStatus(state,0); preserved();
    // Real Ninput recognition, calibrated with the private digitizer. No
    // desktop input or manufactured Windows/recognizer messages.
    TouchInputTest::Device input; input.primary=1;
    RECT client{}; ViewClientRect(state,&client);
    const LONG radius=Scale96(35,ViewDpi(state));
    POINT left{client.right/2-radius,client.bottom/2},right{client.right/2+radius,client.bottom/2};
    using namespace TouchInputTest;
    input.Next(); input.Send(state,{input.Packet(state,1,left,down)});
    input.Next(); input.Send(state,{input.Packet(state,1,left,move),input.Packet(state,2,right,down)});
    for(int n=0;n<3;++n) {
        left.x-=Scale96(15,ViewDpi(state)); right.x+=Scale96(15,ViewDpi(state));
        input.Next(); input.Send(state,{input.Packet(state,1,left,move),input.Packet(state,2,right,move)});
    }
    Check(state.touch.gesture==TouchGesture::Pinch && !IsWindowVisible(editor),"native pinch failed to suspend rename");
    input.Next(); input.Send(state,{input.Packet(state,1,left,up),input.Packet(state,2,right,up)});
    StopTouch(state); AdvanceRowHeight(state,true); preserved();
    ApplyRowHeight(state,initial); AdvanceRowHeight(state,true); preserved();
    // Exercise the independent GPU lane's actual final-frame receipt too.
    tab.Start(); ApplyRowHeight(state,initial+8); SubmitPresentation(state);
    Check(!IsWindowVisible(editor),"presentation zoom did not hide rename before submission");
    std::shared_ptr<const PresentationReceipt> last;
    const bool restored=Eventually([&] {
        if(auto receipt=tab.Receipt()) last=receipt;
        // USER supplies paint and completion messages from the actual owner
        // invalidations/worker; dispatch them through the production handler.
        MSG message{};
        while(PeekMessageW(&message,state.hwnd,WM_PAINT,WM_PAINT,PM_REMOVE)) DispatchMessageW(&message);
        while(PeekMessageW(&message,state.hwnd,kMsgPresentationReady,kMsgPresentationReady,PM_REMOVE)) DispatchMessageW(&message);
        ConsumePresentation(state); return !state.zoomAnimating && IsWindowVisible(editor);
    });
    if(!restored) std::cout<<"RENAME ZOOM completion owner="<<state.zoomAnimating<<" hidden="<<state.rename.zoomHidden
        <<" retarget="<<state.rename.zoomRetargeting<<" pinch="<<state.touchpadPinching<<" manipulation="<<state.manipulationFrameActive
        <<" touch="<<state.touch.active<<" released="<<state.swipeReleased<<" ready="<<(last && last->ready)
        <<" accepted="<<(last && last->accepted)<<" zoom="<<(last && last->zoom)<<" seq="<<state.presentationSequence
        <<" receipt="<<(last ? last->sequence:0)<<" origin="<<state.zoomStartedQpc.QuadPart
        <<" receipt-origin="<<(last ? last->zoomOrigin.QuadPart:0)<<" style="<<state.presentationStyleRevision
        <<" receipt-style="<<(last ? last->styleRevision:0)<<std::endl;
    Check(restored,"compositor completion did not restore rename");
    preserved();
    ApplyRowHeight(state,initial); AdvanceRowHeight(state,true); preserved();
    g_smooth=smooth;
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

static void TestRenameAppearanceImpl(const std::wstring& directory) {
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
        std::filesystem::create_directories(path);
        auto& entry=state.entries.back();
        entry.name=grouped ? L"a longer original filename that wraps across several lines" : L"gpu-accelerated-explorer-view.wh.cpp";
        entry.folder=grouped; entry.path=(path/entry.name).wstring();
        if(entry.folder) std::filesystem::create_directories(entry.path); else std::ofstream(std::filesystem::path(entry.path)).close();
        auto* view=new SelectionFolderView;
        ComPtr<IShellItem> item;
        Check(SUCCEEDED(SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&item))) &&
            SUCCEEDED(item->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&state.folder))),"rename appearance private Shell folder");
        view->folder=state.folder; view->state=&state; view->reenterCapture=false; state.folderView.Attach(view);
        state.selected=state.anchor=state.total-1;
        state.selectedFlags.assign(state.total,0); state.selectedFlags[size_t(state.selected)]=1;
        if(details) {
            ExplorerColumn type; type.key=PKEY_ItemTypeText; type.width=Scale96(350,dpi);
            ExplorerColumn name; name.key=PKEY_FileName; name.width=Scale96(150,dpi);
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
            if(details) {
                if(!(trailing ? state.horizontalScroll<before : state.horizontalScroll>before))
                    std::cout<<"RENAME horizontal reveal dpi="<<dpi<<" rtl="<<rtl<<" grouped="<<grouped
                        <<" trailing="<<trailing<<" before="<<before<<" after="<<state.horizontalScroll
                        <<" lines="<<SendMessageW(state.rename.editor,EM_GETLINECOUNT,0,0)
                        <<" line-height="<<RenameLineHeight(state.rename.editor)<<std::endl;
                Check(trailing ? state.horizontalScroll<before : state.horizontalScroll>before,
                    "rename did not reveal the moved Name column from both horizontal edges");
            }
            RenameAppearanceFixture::Bounds(state);
            RenameAppearanceFixture::TextAlignment(state);
            RenameAppearanceFixture::Prefix(state);
            if(!trailing) {
                RenameAppearanceFixture::SelectionPaint(state);
                RenameAppearanceFixture::FontQuality(state);
            }
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
                    RenameAppearanceFixture::AllLines(state);
                    RECT format{}, after{}; SendMessageW(state.rename.editor,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
                    const auto beforeLines=SendMessageW(state.rename.editor,EM_GETLINECOUNT,0,0);
                    const auto beforeLine=RenameLineHeight(state.rename.editor);
                    RenameAppearanceFixture::PaintLayoutProbe stable;
                    Check(SetWindowSubclass(state.rename.editor,RenameAppearanceFixture::ObservePaintLayout,2,
                        reinterpret_cast<DWORD_PTR>(&stable)),"settled rename formatting observer");
                    for(int n=0;n<3;++n) RepositionRenameEdit(state);
                    RemoveWindowSubclass(state.rename.editor,RenameAppearanceFixture::ObservePaintLayout,2);
                    SendMessageW(state.rename.editor,EM_GETRECT,0,reinterpret_cast<LPARAM>(&after));
                    if(!EqualRect(&format,&after)) std::cout<<"RENAME format dpi="<<dpi<<" rtl="<<rtl<<" mode="<<mode
                        <<" grouped="<<grouped<<" before="<<format.left<<","<<format.top<<","<<format.right<<","<<format.bottom
                        <<" after="<<after.left<<","<<after.top<<","<<after.right<<","<<after.bottom
                        <<" native-lines="<<beforeLines<<"->"<<SendMessageW(state.rename.editor,EM_GETLINECOUNT,0,0)
                        <<" line-height="<<beforeLine<<"->"<<RenameLineHeight(state.rename.editor)
                        <<" text="<<(text[0]==L'a' ? "long":"short")<<" font="<<(face[0]==L'A' ? "Arial":"Consolas")<<std::endl;
                    Check(EqualRect(&format,&after),"rename centering repeatedly inset the native formatting rectangle");
                    Check(!stable.formats && !stable.positions && !stable.fonts,
                        "settled rename layout repeatedly reformatted or repositioned the editor");
                }
                SendMessageW(state.rename.editor,WM_SETFONT,reinterpret_cast<WPARAM>(state.rename.font),TRUE);
                DeleteObject(font);
            }
            if(grid && mode==FVM_ICON && !grouped && !trailing && dpi!=144)
                RenameAppearanceFixture::Zoom(tab);
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
    std::cout<<"PASS: native rename colors/smoothing, actual prefix coordinates and basename selection, API selection changes without layout, full wrapped-name growth, wheel/native pinch/compositor hide/restore with preserved text/selection and updated fonts; GPU selection and real resize/reveal in grouped/ungrouped LTR/RTL views at 96/144/192 DPI\n";
}

static void TestRenameAppearance(const std::wstring& directory) {
    TestRenameAppearanceImpl(directory);
    const auto manifest=std::filesystem::path(__FILE__).parent_path()/L"rename-native-controls.manifest";
    ACTCTXW settings{sizeof(settings)}; settings.lpSource=manifest.c_str();
    const HANDLE context=CreateActCtxW(&settings);
    Check(context!=INVALID_HANDLE_VALUE,"themed native control activation context");
    ULONG_PTR cookie=0;
    if(!ActivateActCtx(context,&cookie)) {ReleaseActCtx(context); Check(false,"activate themed native controls");}
    try {TestRenameAppearanceImpl(directory);}
    catch(...) {DeactivateActCtx(0,cookie); ReleaseActCtx(context); throw;}
    DeactivateActCtx(0,cookie); ReleaseActCtx(context);
    std::cout<<"PASS: rename appearance exercised both USER32 and themed COMCTL32 native controls\n";
}
