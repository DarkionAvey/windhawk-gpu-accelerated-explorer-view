// Real filesystem names, separate DirectWrite measurement, and the renderer's
// shared item layout. No synthetic mouse messages or desktop input are needed.
static void TestItemDragHitZones(const std::wstring& fixture) {
    StyleFixture::Session style;
    const auto directory=JoinPath(fixture,L"drag-hit-content");
    Check(std::filesystem::create_directory(directory),"drag hit directory");
    const auto path=JoinPath(directory,L"test");
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(file!=INVALID_HANDLE_VALUE,"drag hit file"); CloseHandle(file);
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"drag hit fonts");
    for(UINT dpi:{96u,144u}) for(bool rtl:{false,true})
        for(UINT mode:{FVM_DETAILS,FVM_LIST,FVM_SMALLICON,FVM_ICON,FVM_TILE,FVM_CONTENT}) {
        Regression::Scope scope("drag hit dpi="+std::to_string(dpi)+" rtl="+std::to_string(rtl)+" mode="+std::to_string(mode));
        StyleFixture::dpi=dpi;
        ViewState state;
        state.hwnd=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"private drag bounds",
            WS_POPUP,-32000,-32000,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state.hwnd!=nullptr,"drag hit window");
        state.nativeHwnd=state.frameHwnd=state.activeTabHost=state.background->hwnd=state.hwnd;
        state.folderPath=directory; state.visible=state.inScope=state.detailsViewActive=true;
        state.rtlLayout=rtl; state.paintWidth=900.f;
        ApplyViewLayout(state,{mode,48},false); UpdateMetrics(state); UpdateColumns(state); UpdateRefreshRate(state);
        QueueDirectoryRefresh(state,true);
        auto names=WaitForComplete(*state.background,state.directoryToken);
        PublishDirectoryResult(*state.background,std::move(names)); ReceiveDirectoryResults(state); StopRowTransition(state);
        Check(state.total==1 && EntryLabel(state.entries[0])==L"test","drag fixture real filename");
        if(!state.gridView) state.rowPx=120.f*dpi/96.f;
        RebuildGroupSections(state); UpdateColumns(state);
        Check(EnsureTarget(state),"drag hit text formats");
        const RowVisual row{state.headerPx,DisplayRowHeight(state,0),1.f,DocumentRowLeft(state,0),state.itemWidth};
        const auto item=LayoutItem(state,row);
        const auto hit=[&](float x,float y) {
            return PointStartsItemDrag(state,0,POINT{LogicalX(state,LONG(x)),LONG(y)});
        };
        for(bool selected:{false,true}) {
            state.selectedFlags.assign(1,selected); state.selected=selected?0:-1;
            Check(hit((item.icon.left+item.icon.right)*.5f,(item.icon.top+item.icon.bottom)*.5f),
                "icon box requires prior selection");
            if(!state.gridView) {
                ComPtr<IDWriteTextLayout> independent;
                Check(SUCCEEDED(g_writeFactory->CreateTextLayout(L"test",4,state.textFormat.Get(),
                    10000.f,1000.f,&independent)),"independent filename measurement");
                DWRITE_TEXT_METRICS measured{}; Check(SUCCEEDED(independent->GetMetrics(&measured)),"filename metrics");
                Check(measured.width>0.f && item.text.right-item.text.left>measured.width+100.f,
                    "fixture has no wide blank Name area");
                for(float y:{row.top+1.f,row.top+row.height*.5f,row.top+row.height-1.f}) {
                    Check(hit(item.text.left+measured.width*.5f,y),"list filename does not own full tall row height");
                    Check(hit((item.icon.left+item.icon.right)*.5f,y),"list icon does not own full tall row height");
                    Check(hit(item.text.left+measured.width+60.f,y)==selected,"Name padding must drag only an already-selected item");
                }
            } else {
                Check(hit((item.icon.left+item.icon.right)*.5f,item.icon.top-1.f)==selected,
                    "grid icon padding must drag only an already-selected item");
                const float outside=item.cell.left+1.f;
                Check(hit(outside,(item.icon.top+item.icon.bottom)*.5f)==selected,"grid cell padding must drag only an already-selected item");
                const auto text=CurrentSelectionRect(state,0,false,nullptr,true);
                Check(text.right>text.left && text.bottom>text.top,"grid filename has no rendered box");
                Check(hit((text.left+text.right)*.5f,(text.top+text.bottom)*.5f),"grid filename box is not draggable");
                Check(hit(text.right+2.f,(text.top+text.bottom)*.5f)==selected,"grid filename padding must drag only an already-selected item");
            }
        }
        UnregisterAnimationWindow(state.hwnd); Stop(state.background); DropTarget(state); DestroyWindow(state.hwnd);
    }
    g_writeFactory.Reset();
    std::cout<<"PASS: selected/unselected icon and measured filename drag targets, tall list rows and blank grid/Name padding, 96/144 DPI and LTR/RTL\n";
}
