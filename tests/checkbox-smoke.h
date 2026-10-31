// Native private ExplorerBrowser selection and Windows' touch recognizer.
// No desktop input, global Explorer setting writes, clipboard or focus changes.
namespace CheckboxTest {
POINT Center(ViewState& state,const D2D1_RECT_F& box) {
    return {LogicalX(state,LONG((box.left+box.right)*.5f)),LONG((box.top+box.bottom)*.5f)};
}
void Tap(TouchInputTest::Device& input,ViewState& state,POINT point) {
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,TouchInputTest::down)});
    input.Next(40); input.Send(state,{input.Packet(state,input.primary,point,TouchInputTest::up)});
}
void RemoveFile(HWND owner,const std::filesystem::path& path) {
    std::wstring names=path.wstring(); names.push_back(L'\0');
    SHFILEOPSTRUCTW operation{}; operation.hwnd=owner; operation.wFunc=FO_DELETE;
    operation.pFrom=names.c_str(); operation.fFlags=FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_SILENT;
    Check(SHFileOperationW(&operation)==0 && !operation.fAnyOperationsAborted,"private Shell file removal");
}
void Selection(const std::wstring& fixture) {
    const auto folder=std::filesystem::path(fixture)/L"checkbox-native";
    std::filesystem::create_directory(folder);
    struct Cleanup {
        std::filesystem::path folder;
        ~Cleanup() {
            // Destroyed after the browser closes. Remove only our six files
            // and the now-empty folder, leaving the shared fixture unchanged.
            std::error_code error;
            for(int i=0;i<6;++i) std::filesystem::remove(folder/(L"item"+std::to_wstring(i)+L".txt"),error);
            std::filesystem::remove(folder/L"inserted.txt",error);
            std::filesystem::remove(folder,error);
        }
    } cleanup{folder};
    for(int i=0;i<6;++i) {
        const auto file=CreateFileW((folder/(L"item"+std::to_wstring(i)+L".txt")).c_str(),
            GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"checkbox private file"); CloseHandle(file);
    }
    ProviderSelectionFixture::Browser browser;
    browser.Navigate(folder.wstring(),6);
    auto& state=browser.state;
    FocusFixture::Session focus(browser.frame,state.nativeHwnd);
    Check(SetWindowSubclass(state.hwnd,+[](HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR)->LRESULT {
        if ((message>=kMsgSettings && message<=kMsgTouchDrag && message!=kMsgShellItemCreated) ||
            message==WM_CAPTURECHANGED || message==WM_CONTEXTMENU || message==WM_TIMER ||
            (message==WM_PAINT && State(hwnd) && State(hwnd)->presenter))
            return OverlayProc(hwnd,message,wp,lp);
        return DefSubclassProc(hwnd,message,wp,lp);
    },307,0),"checkbox production message receiver");
    TouchInputTest::Device input;
    TouchInputTest::Reset(state,input);
    state.clickPolicy.checkSelect=true;
    ClearSelectionForFolderMenu(state);
    const auto rowPoint=[&](int row) {return Center(state,LayoutItem(state,LayoutRowVisual(state,row,1.f)).checkbox);};
    const auto count=[&] {
        int selected=-1;
        Check(SUCCEEDED(state.folderView->ItemCount(SVGIO_SELECTION,&selected)),"checkbox native selection count");
        return selected;
    };
    const auto pump=[&] {
        MSG message{};
        while(PeekMessageW(&message,state.hwnd,kMsgAdoptNativeSelectAll,kMsgAdoptNativeSelectAll,PM_REMOVE))
            OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
        browser.Pump();
        ReceiveDirectoryResults(state);
        ConsumePresentation(state);
    };
    Tap(input,state,rowPoint(0));
    Check(count()==1 && state.selectedFlags[0],"native touch checkbox failed to select item");
    Tap(input,state,rowPoint(1));
    Check(count()==2 && state.selectedFlags[0] && state.selectedFlags[1],"checkbox replaced a disjoint selection");
    Tap(input,state,rowPoint(0));
    Check(count()==1 && !state.selectedFlags[0] && state.selectedFlags[1],"checkbox failed to deselect one item");
    Tap(input,state,rowPoint(1)); Tap(input,state,rowPoint(1));
    Check(count()==1 && !state.optimisticStartedAt && state.folderPath==folder.wstring(),
        "rapid checkbox taps activated a file");
    const auto bodyPoint=[&](int row) {
        const auto item=LayoutItem(state,LayoutRowVisual(state,row,1.f));
        return POINT{LogicalX(state,LONG(item.text.left+state.fontPx)),LONG((item.text.top+item.text.bottom)*.5f)};
    };
    Check(ModifierFixture::active!=nullptr,"checkbox modifier API fixture");
    ModifierFixture::active[VK_CONTROL]=0x80;
    Tap(input,state,bodyPoint(0));
    Check(count()==2 && state.selectedFlags[0] && state.selectedFlags[1],"Ctrl item click did not check both items");
    Tap(input,state,bodyPoint(1));
    Check(count()==1 && state.selectedFlags[0] && !state.selectedFlags[1],"Ctrl item click did not uncheck one item");
    ModifierFixture::active[VK_CONTROL]=0;
    ClearSelectionForFolderMenu(state); TouchInputTest::Reset(state,input);
    const POINT marqueeStart{LogicalX(state,LONG(ListStartX(state)+4.f)),LONG(state.headerPx+DocumentHeight(state)+state.rowPx)};
    const POINT marqueeEnd{LogicalX(state,LONG(NameTextOrigin(state)+100.f)),LONG(state.headerPx+state.rowPx*.5f)};
    input.Next(); input.Send(state,{input.Packet(state,input.primary,marqueeStart,TouchInputTest::down)});
    input.Next(); input.Send(state,{input.Packet(state,input.primary,marqueeEnd,TouchInputTest::move)});
    Check(state.marqueeActive,"checkbox native marquee did not start");
    input.Next(); input.Send(state,{input.Packet(state,input.primary,marqueeEnd,TouchInputTest::up)});
    Check(Eventually([&] {pump(); return count()==6 && std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)==6;}),
        "marquee selection did not check the selected items");
    StopTouch(state); ClearSelectionForFolderMenu(state); Tap(input,state,rowPoint(1));
    const auto header=Center(state,HeaderSelectionCheckbox(state));
    Check(SelectionCheckboxAt(state,header)==-2,"Name header checkbox hit");
    PresentationFixture::Hold checkerHold(state.hwnd);
    Check(StartPresentation(state),"checkbox background presentation worker");
    struct Presenter {
        ViewState& state;
        ~Presenter() {
            ReleasePresentation(state,true); state.background->Stop();
            if(state.background->presentation.thread) WaitForSingleObject(state.background->presentation.thread,10000);
            state.presenter.reset();
        }
    } presenter{state};
    Redraw(state);
    Check(WaitForSingleObject(PresentationFixture::entered,10000)==WAIT_OBJECT_0,"checkbox checker did not reach background lane");
    Tap(input,state,header);
    Check(state.checkboxHeaderActionPending && count()==1 && HeaderSelectionCheckboxState(state)==-3,
        "header scanned or waited on the UI while its checker was withheld");
    checkerHold.Release();
    const bool selectedAll=Eventually([&] {pump(); return count()==6 &&
        std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)==6;});
    if(!selectedAll) std::cout<<"CHECKBOX background timeout native="<<count()<<" gpu="
        <<std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)<<" pending="<<state.checkboxHeaderActionPending
        <<" header="<<HeaderSelectionCheckboxState(state)<<" total="<<state.total<<" policy="<<state.clickPolicy.checkSelect
        <<" visible="<<state.visible<<" failure="<<std::hex<<state.graphicsFailure<<std::dec
        <<" sequence="<<state.presentationSequence<<" accepted="<<state.presentationAcceptedSequence<<std::endl;
    Check(selectedAll,
        "real Shell Select All command did not select every item");
    Tap(input,state,header);
    Check(Eventually([&] {pump(); return count()==0 && std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)==0;}),
        "header checkbox failed to clear native selection");
    // Resize the actual private window to overflow: fitting directories use
    // one-finger marquee by design, while overflowing directories must pan.
    Check(SetWindowPos(state.hwnd,nullptr,0,0,1000,120,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"checkbox viewport resize");
    UpdateMetrics(state); ClampScroll(state);
    Check(MaxScrollPx(state)>0.f,"checkbox pan fixture does not overflow");
    const POINT start=rowPoint(0), finish{start.x,start.y+LONG(state.rowPx*3.f)};
    input.Next(); input.Send(state,{input.Packet(state,input.primary,start,TouchInputTest::down)});
    input.Next(); input.Send(state,{input.Packet(state,input.primary,finish,TouchInputTest::move)});
    input.Next(); input.Send(state,{input.Packet(state,input.primary,finish,TouchInputTest::up)});
    Check(count()==0,"movement starting at a checkbox toggled an item");
    StopTouch(state);
    SetWindowPos(state.hwnd,nullptr,0,0,1000,700,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    UpdateMetrics(state); ClampScroll(state);
    TouchInputTest::Reset(state,input);
    HWND peer=CreateWindowExW(0,L"STATIC",L"private capture owner",WS_CHILD,0,0,1,1,
        browser.frame,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(peer!=nullptr,"checkbox capture peer");
    input.Next(); input.Send(state,{input.Packet(state,input.primary,header,TouchInputTest::down)});
    Check(state.checkboxPress==-2 && GetCapture()==state.hwnd,"header checkbox did not own its press");
    SetCapture(peer);
    input.Next(); input.Send(state,{input.Packet(state,input.primary,header,TouchInputTest::up)});
    Check(count()==0 && state.checkboxPress==-1,"stolen checkbox capture committed selection");
    if(GetCapture()==peer) ReleaseCapture(); DestroyWindow(peer);
    const auto awaitRows=[&](int total,int check) {
        const bool settled=Eventually([&] {
            pump(); int nativeTotal=-1;
            state.folderView->ItemCount(SVGIO_ALLVIEW,&nativeTotal);
            return state.total==total && !state.directoryPending &&
                !state.watchedChangePending && state.checkboxSummaryTotal==total &&
                state.checkboxSelection.SharesStorage(state.selectedFlags) && HeaderSelectionCheckboxState(state)==check;
        },30000);
        if(!settled) {
            int nativeTotal=-1; state.folderView->ItemCount(SVGIO_ALLVIEW,&nativeTotal);
            std::cout<<"CHECKBOX membership timeout expected="<<total<<","<<check<<" actual="<<state.total<<","<<HeaderSelectionCheckboxState(state)
                <<" native="<<nativeTotal<<" selected="<<count()<<" gpu-selected="<<std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)
                <<" pending="<<state.checkboxHeaderActionPending<<" directory="<<state.directoryPending<<" watch="<<state.watchedChangePending
                <<" policy="<<state.clickPolicy.checkSelect<<" visible="<<state.visible<<std::endl;
        }
        Check(settled,"checkbox membership/selection summary did not follow the real directory watch");
    };
    const auto refreshNative=[&](int total) {
        ComPtr<IShellView> view;
        Check(SUCCEEDED(state.shellBrowser->QueryActiveShellView(&view)) && SUCCEEDED(view->Refresh()),"private native folder refresh");
        Check(Eventually([&] {pump(); int actual=-1; state.folderView->ItemCount(SVGIO_ALLVIEW,&actual); return actual==total;},30000),
            "native Shell refresh did not adopt filesystem membership");
    };
    Tap(input,state,header); awaitRows(6,1);
    RemoveFile(browser.frame,folder/L"item5.txt");
    // Shell's delete operation clears its selection in this private control.
    // The header must reflect that rather than retaining its last clicked state.
    awaitRows(5,0);
    refreshNative(5);
    Tap(input,state,header); awaitRows(5,1);
    const auto inserted=folder/L"inserted.txt";
    HANDLE newFile=CreateFileW(inserted.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(newFile!=INVALID_HANDLE_VALUE,"checkbox inserted file"); CloseHandle(newFile);
    SHChangeNotify(SHCNE_CREATE,SHCNF_PATHW|SHCNF_FLUSH,inserted.c_str(),nullptr);
    awaitRows(6,0);
    const int uncheckedInserted=GpuRowForNativePath(state,inserted.wstring());
    Check(uncheckedInserted>=0 && !state.selectedFlags[size_t(uncheckedInserted)],"new item inherited a checked box");

    // Change selection in the actual native view while the overlay is off.
    // Returning must import its checked item through the production hint path.
    SetActive(state.hwnd,false);
    Check(!state.visible,"checkbox native view toggle");
    refreshNative(6);
    Check(SUCCEEDED(state.folderView->SetCurrentFolderFlags(FWF_AUTOCHECKSELECT,FWF_AUTOCHECKSELECT)),"private native checkbox option");
    const int insertedRow=GpuRowForNativePath(state,inserted.wstring());
    Check(insertedRow>=0,"checkbox inserted identity");
    PITEMID_CHILD raw=nullptr;
    Check(SUCCEEDED(EntryChildPidl(state,state.entries[size_t(insertedRow)],&raw)) && raw,"native checkbox child");
    OwnedPidl child(raw); PCUITEMID_CHILD selectedChild=child.get();
    Check(SUCCEEDED(state.folderView->SelectAndPositionItems(1,&selectedChild,nullptr,
        SVSI_SELECT|SVSI_DESELECTOTHERS|SVSI_NOTAKEFOCUS)),"native checkbox selection");
    SetActive(state.hwnd,true); state.clickPolicy.checkSelect=true; Redraw(state);
    Check(Eventually([&] {
        pump(); const int row=GpuRowForNativePath(state,inserted.wstring());
        return state.visible && row>=0 && state.selectedFlags[size_t(row)] &&
            std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)==1 && HeaderSelectionCheckboxState(state)==0;
    },30000),"native checked selection did not return to overlay checkboxes");
    Tap(input,state,header); awaitRows(6,1);
    for(int i=0;i<5;++i) RemoveFile(browser.frame,folder/(L"item"+std::to_wstring(i)+L".txt"));
    RemoveFile(browser.frame,inserted);
    awaitRows(0,-1);
    Check(state.checkboxSelectedCount==0 && SelectionCheckboxAt(state,header)==-1,
        "empty directory retained a checked or clickable Name checkbox");
    newFile=CreateFileW((folder/L"item0.txt").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(newFile!=INVALID_HANDLE_VALUE,"checkbox repopulated file"); CloseHandle(newFile);
    SHChangeNotify(SHCNE_CREATE,SHCNF_PATHW|SHCNF_FLUSH,(folder/L"item0.txt").c_str(),nullptr);
    awaitRows(1,0);
    std::cout<<"CHECKBOX native Shell selection, disjoint toggles, header and recognizer cancellation passed"<<std::endl;
}
void Pixels() {
    for(UINT dpi:{96u,192u}) for(bool rtl:{false,true}) for(UINT mode:{UINT(FVM_DETAILS),UINT(FVM_LIST),UINT(FVM_SMALLICON),UINT(FVM_ICON)}) {
        StyleFixture::Session style; StyleFixture::dpi=dpi;
        PresentationFixture::Tab tab(L"checkbox pixels",6,rtl,IsIconViewMode(mode));
        auto& view=tab.state;
        ApplyViewLayout(view,{mode,48},false); view.cachedNativeMode=mode;
        view.clickPolicy.checkSelect=true;
        UpdateMetrics(view); UpdateColumns(view);
        view.selectedFlags.Fill(0); view.hovered=-1;
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency); view.qpcFrequency=double(frequency.QuadPart);
        TouchInputTest::RenderedTouch frame;
        AnimationClockFixture::Freeze clock;
        const auto same=[](D2D1_RECT_F a,D2D1_RECT_F b) {return memcmp(&a,&b,sizeof(a))==0;};
        const auto geometry=LayoutItem(view,LayoutRowVisual(view,0,1.f));
        const float width=view.itemWidth;
        const auto before=frame.Capture(view);
        view.hovered=0; const auto hovered=frame.Capture(view);
        const auto box=Center(view,geometry.checkbox);
        const int pixelWidth=frame.renderer.backbuffer->GetPixelSize().width;
        unsigned changes=0;
        const int extent=std::max(1,Scale96(10,dpi));
        for(int y=box.y-extent;y<box.y+extent;++y) for(int x=box.x-extent;x<box.x+extent;++x) {
            const size_t offset=(size_t(y)*pixelWidth+x)*4;
            changes+=memcmp(before.data()+offset,hovered.data()+offset,4)!=0;
        }
        Check(changes>20,"hovered checkbox did not appear immediately at its mirrored hit target");
        const auto edge=[&](int row) {
            const auto item=LayoutItem(view,LayoutRowVisual(view,row,1.f));
            const auto point=Center(view,item.checkbox);
            return (size_t(LONG(item.checkbox.top+(item.checkbox.right-item.checkbox.left)/40.f))*pixelWidth+point.x)*4;
        };
        SetTouchInputMode(view,true); const auto touch=frame.Capture(view);
        for(int i:VisibleItemRows(view,0)) Check(touch[edge(i)+3]>100,"touch checkbox did not appear immediately");
        view.selectedFlags[0]=1; view.selected=0;
        SetTouchInputMode(view,false); view.hovered=-1; const auto mouse=frame.Capture(view);
        for(int i:VisibleItemRows(view,0))
            Check(i==0 ? mouse[edge(i)+3]>100 : mouse[edge(i)+3]==before[edge(i)+3],
                "mouse checkbox visibility did not immediately follow actual selection");
        view.hovered=0; frame.Capture(view); view.hovered=-1; auto checked=frame.Capture(view);
        Check(checked[edge(0)+3]>100,"checked item disappeared after hover left");
        for(int palette=0;palette<3;++palette) {
            StyleFixture::appBg=palette==0 ? ABI::Windows::UI::Color{255,24,24,24} :
                ABI::Windows::UI::Color{255,250,250,250};
            StyleFixture::appFg=palette==0 ? ABI::Windows::UI::Color{255,240,240,240} : ABI::Windows::UI::Color{255,20,20,20};
            StyleFixture::text=RGB(255,255,0);
            StyleFixture::highContrast=palette==2; StyleFixture::window=RGB(10,20,30);
            const COLORREF background=palette==0 ? RGB(24,24,24) : palette==1 ? RGB(250,250,250) : StyleFixture::window;
            RefreshNativePalette(view,true);
            const auto probes=std::array<unsigned,3>{StyleFixture::activations,StyleFixture::themeQueries,StyleFixture::systemColorQueries};
            checked=frame.Capture(view);
            Check(probes==std::array<unsigned,3>{StyleFixture::activations,StyleFixture::themeQueries,StyleFixture::systemColorQueries},
                "checkbox paint queried a color provider");
            const size_t offset=edge(0);
            const auto distance=[&](COLORREF color) {return abs(int(checked[offset])-GetBValue(color))+
                abs(int(checked[offset+1])-GetGValue(color))+abs(int(checked[offset+2])-GetRValue(color));};
            Check(distance(view.nativeTextColor)<distance(background),"checked border did not retain the normal theme foreground");
        }
        D2D1_RECT_F tick{}; float tickArea=0.f;
        Check(frame.renderer.selectionCheckGlyph && SUCCEEDED(frame.renderer.selectionCheckGlyph->GetBounds(nullptr,&tick)) &&
            SUCCEEDED(frame.renderer.selectionCheckGlyph->ComputeArea(nullptr,&tickArea)) && tickArea>50.f &&
            tick.right-tick.left>13.f && tick.bottom-tick.top>11.f &&
            fabsf((tick.left+tick.right)*.5f-10.f)<.5f && fabsf((tick.top+tick.bottom)*.5f-10.f)<.5f,
            "checkbox tick is too small, thin or off center");
        view.selectedFlags[0]=0; view.selected=-1; const auto unselected=frame.Capture(view);
        Check(abs(int(unselected[edge(0)])-int(checked[edge(0)]))+
            abs(int(unselected[edge(0)+1])-int(checked[edge(0)+1]))+
            abs(int(unselected[edge(0)+2])-int(checked[edge(0)+2]))>50,"unchecked unhovered checkbox did not disappear immediately");
        const auto after=LayoutItem(view,LayoutRowVisual(view,0,1.f));
        Check(same(geometry.icon,after.icon) && same(geometry.text,after.text) && width==view.itemWidth,
            "checkbox input mode moved item geometry");
        Check(SelectionCheckboxAt(view,box)==0,"checkbox paint and hit geometry disagree");
        if(view.gridView) Check(geometry.checkbox.left<geometry.cell.left+Scale96(10,dpi),"grid checkbox lost its leading corner");
        view.clickPolicy.checkSelect=false;
        Check(SelectionCheckboxAt(view,box)==-1,"disabled checkboxes retained an input target");
        const auto disabled=LayoutItem(view,LayoutRowVisual(view,0,1.f));
        if(!view.gridView) Check(disabled.text.left<after.text.left,"checkbox gutter was not reserved in list geometry");
    }
    std::cout<<"CHECKBOX instant visibility, normal theme borders, bold centered ticks, stable layout, DPI and RTL passed"<<std::endl;
}
void TouchGridTargets(const std::wstring& fixture) {
    for(UINT dpi:{96u,192u}) for(bool rtl:{false,true}) for(UINT mode:{UINT(FVM_SMALLICON),UINT(FVM_ICON)}) {
        StyleFixture::Session style; StyleFixture::dpi=dpi;
        PresentationFixture::Tab tab(L"touch checkbox targets",6,rtl,true);
        auto& state=tab.state;
        auto* native=SelectionUiFixture::Setup(tab,fixture);
        TouchInputTest::Install(state);
        FocusFixture::Session focus(tab.frame,state.hwnd);
        MenuFixture::Session menus(state.hwnd);
        ComPtr<SelectionCommandMenu> commands; commands.Attach(new SelectionCommandMenu); commands->defaultSelection=true;
        ComPtr<TouchInputTest::ShellView> shell; shell.Attach(new TouchInputTest::ShellView);
        shell->hwnd=state.nativeHwnd; shell->menu=commands; shell->selection=native; shell->parent=state.directoryFolderIdentity;
        native->commandView=shell;
        auto* browser=new TimingShellBrowser; browser->view=shell; state.shellBrowser.Attach(browser);
        state.shellOleInitialized=true;
        ApplyViewLayout(state,{mode,48},false); state.cachedNativeMode=mode;
        state.clickPolicy.checkSelect=state.clickPolicy.singleClick=true;
        UpdateMetrics(state); UpdateColumns(state);
        TouchInputTest::Device input; TouchInputTest::Reset(state,input);
        SetTouchInputMode(state,true);
        const auto item=LayoutItem(state,LayoutRowVisual(state,0,1.f));
        const auto hit=SelectionCheckboxHitRect(state,item);
        Check(hit.left>=item.cell.left && hit.top>=item.cell.top && hit.right<=item.cell.right && hit.bottom<=item.cell.bottom &&
            hit.right<=item.cell.left+(item.cell.right-item.cell.left)*.5f+.01f,"touch checkbox escaped its cell or consumed the opening area");
        Check(hit.right-hit.left>item.checkbox.right-item.checkbox.left,"touch grid checkbox target was not enlarged");
        const POINT extended{LogicalX(state,LONG(hit.right-1.f)),LONG((hit.top+hit.bottom)*.5f)};
        Check(SelectionCheckboxAt(state,extended)==0,"enlarged touch target lost its mirrored hit identity");
        Tap(input,state,extended); WaitForPreparedSelection(state);
        Check(state.selectedFlags[0] && commands->calls.empty(),"touch margin opened a single-click item instead of checking it");
        Tap(input,state,extended); WaitForPreparedSelection(state);
        Check(!state.selectedFlags[0] && commands->calls.empty(),"repeated touch margin tap activated rather than toggled");
        const POINT opening{LogicalX(state,LONG(item.cell.right-Scale96(6,dpi))),LONG((item.cell.top+item.cell.bottom)*.5f)};
        Check(SelectionCheckboxAt(state,opening)==-1 && RowFromPoint(state,opening.y,opening.x)==0,"touch checkbox left no opening target");
        Tap(input,state,opening); WaitForPreparedSelection(state);
        Check(commands->calls.size()==1,"single-click activation outside the checkbox target stopped working");
        commands->calls.clear(); state.clickPolicy.singleClick=false;
        TouchInputTest::Reset(state,input);
        Tap(input,state,opening); Tap(input,state,opening); WaitForPreparedSelection(state);
        Check(commands->calls.size()==1,"double-tap activation outside the checkbox target stopped working");
        SetTouchInputMode(state,false);
        Check(SelectionCheckboxAt(state,extended)==-1,"touch hit padding remained active for mouse input");
        StopTouch(state);
    }
    std::cout<<"CHECKBOX bounded touch grid targets, single-click safety and separate double-tap opening; DPI/RTL passed"<<std::endl;
}
}
static void TestSelectionCheckboxes(const std::wstring& fixture) {
    const auto fonts=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"checkbox fonts");
    struct Restore {ComPtr<IDWriteFactory> fonts; ~Restore() {g_writeFactory=fonts;}} restore{fonts};
    TouchFixture::Session input;
    PointerFixture::Session pointer;
    ModifierFixture::Session modifiers;
    FrameInputFixture::Session queue;
    SHELLSTATE native{}; SHGetSetSettings(&native,SSF_AUTOCHECKSELECT,FALSE);
    Check(ReadShellClickPolicy().checkSelect==(native.fAutoCheckSelect!=0),"checkbox policy differs from Windows");
    CheckboxTest::Selection(fixture);
    CheckboxTest::Pixels();
    CheckboxTest::TouchGridTargets(fixture);
}
