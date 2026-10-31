// Native Ninput recognition and a private ExplorerBrowser. Mouse geometry and
// lifecycle are component checks; no desktop input or fabricated messages.
static void TestAdditionalGestures(const std::wstring& fixture) {
    using namespace TouchInputTest;
    const auto root=std::filesystem::path(fixture+L"-additional-gestures");
    std::filesystem::create_directories(root/L"Folder");
    for(const auto* name:{L"one.txt",L"two.txt"}) {
        HANDLE file=CreateFileW((root/name).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"gesture private file"); CloseHandle(file);
    }
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code ec;
            for(const auto* name:{L"one.txt",L"two.txt",L"private.zip",L"Folder"}) std::filesystem::remove(root/name,ec);
            std::filesystem::remove(root,ec);
        }
    } cleanup{root};
    const bool upSetting=g_doubleClickUp.exchange(true),tabSetting=g_middleClickNewTab.exchange(true),
        touchSetting=g_twoFingerContextMenu.exchange(true);
    const int margin=g_edgeMargin96.exchange(40);
    struct Restore {bool up,tab,touch; int margin; ~Restore() {
        g_doubleClickUp=up; g_middleClickNewTab=tab; g_twoFingerContextMenu=touch; g_edgeMargin96=margin;
    }} restore{upSetting,tabSetting,touchSetting,margin};
    TouchFixture::Session nativeInput;
    PointerFixture::Session pointer;
    ModifierFixture::Session modifiers;
    StyleFixture::Session metrics;
    ProviderSelectionFixture::Browser host;
    host.Navigate(root.wstring(),3);
    auto& state=host.state;
    Install(state);
    FocusFixture::Session focus(host.frame,state.hwnd);
    MenuFixture::Session menus(state.hwnd);
    Device input;
    int file=-1,folder=-1;
    for(int row=0;row<state.total;++row) (state.entries[size_t(row)].folder ? folder : file)=row;
    Check(file>=0 && folder>=0,"gesture fixture rows");
    const auto tap=[&](POINT first,bool staggered,bool canceled,unsigned fingers=2) {
        Reset(state,input);
        POINT second=first;
        RECT client{}; GetClientRect(state.hwnd,&client);
        second.x+=first.x<client.right/2 ? Scale96(40,ViewDpi(state)) : -Scale96(40,ViewDpi(state));
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,down)});
        input.Next();
        if(fingers==3) {
            POINT third=second; third.y+=Scale96(40,ViewDpi(state));
            input.Send(state,{input.Packet(state,input.primary,first,move),
                input.Packet(state,input.primary+1,second,down),input.Packet(state,input.primary+2,third,down)});
            input.Next(40); input.Send(state,{input.Packet(state,input.primary,first,up),
                input.Packet(state,input.primary+1,second,up),input.Packet(state,input.primary+2,third,up)});
        } else {
            input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,down)});
            input.Next(40);
            const auto release=up | (canceled ? POINTER_FLAG_CANCELED : 0u);
            input.Send(state,{input.Packet(state,input.primary,first,staggered ? move : release),
                input.Packet(state,input.primary+1,second,release)});
            if(staggered) {input.Next(); input.Send(state,{input.Packet(state,input.primary,first,release)});}
        }
        WaitForPreparedSelection(state); host.Pump();
        Check(Eventually([&] {
            AdvanceTouchInertia(state);
            return !state.touch.inertia && !state.touch.active;
        }),"completed multi-finger tap retained an active contact or inertia");
    };
    for(UINT dpi:{96u,144u}) for(bool rtl:{false,true}) for(UINT mode:{FVM_DETAILS,FVM_LIST,FVM_ICON}) {
        StyleFixture::dpi=dpi; state.rtlLayout=rtl;
        ApplyViewLayout(state,{mode,48},false); UpdateMetrics(state); UpdateColumns(state);
        StopRowTransition(state); SetScrollImmediate(state,0); SetHorizontalScroll(state,0);
        const auto item=LayoutItem(state,LayoutRowVisual(state,file,1.f));
        const POINT body{LogicalX(state,LONG((item.text.left+item.text.right)*.5f)),LONG((item.text.top+item.text.bottom)*.5f)};
        const RECT viewport=ContentViewport(state,true);
        RECT client{}; GetClientRect(state.hwnd,&client);
        const LONG below=LONG((state.headerPx+DocumentHeight(state)+viewport.bottom)*.5f);
        const std::array<POINT,4> points{body,POINT{client.right/2,below},POINT{1,body.y},POINT{client.right-1,body.y}};
        Check(!EmptySpaceAt(state,body),"item classified as gesture background");
        Check(!EmptySpaceAt(state,POINT{body.x,0}),"header classified as gesture background");
        for(size_t n=0;n<points.size();++n) {
            Check(n==0 || EmptySpaceAt(state,points[n]),"blank space or symmetric margin missed shared hit testing");
            ClearSelectionForFolderMenu(state); WaitForPreparedSelection(state);
            const auto before=MenuFixture::calls;
            tap(points[n],n%2!=0,false);
            Check(MenuFixture::calls==before+1,"native two-finger tap did not route exactly one right-click menu");
            Check(MenuFixture::target==state.nativeHwnd && MenuFixture::menuPoint.x!=-1,
                "touch menu missed the native mouse context-menu route");
            Check(n==0 ? state.selectedFlags[size_t(file)]!=0 : state.selected<0,
                "two-finger tap chose a different item or retained background selection");
        }
        const auto before=MenuFixture::calls;
        tap(body,false,true); tap(body,false,false,3);
        g_twoFingerContextMenu=false; tap(body,false,false); g_twoFingerContextMenu=true;
        Check(MenuFixture::calls==before,"canceled, three-finger or disabled tap opened a menu");
        state.mousePressBackground=state.mouseClickBackground=true;
        ResetMouseGestureForNavigation(state);
        Check(!state.mousePressBackground && !state.mouseClickBackground,
            "navigation retained an additional mouse gesture");
    }
    // A tabless ExplorerBrowser must never fall back to opening a window.
    // Resolve the prepared selection through native Shell interfaces as well.
    TabActivationFixture::Session activation;
    Open(state,file,true);
    Check(!TabActivationFixture::calls,"new-tab gesture activated a regular file");
    Open(state,folder,true); WaitForPreparedSelection(state);
    Check(!TabActivationFixture::calls && state.selected==folder && state.deferredSelectionKeys.empty(),
        "tabless host launched a window or retained a pending command");
    ComPtr<IShellView> native;
    Check(SUCCEEDED(state.shellBrowser->QueryActiveShellView(&native)) && native &&
        InvokeNativeFileKey(state,native.Get(),{VK_RETURN,0,true})==HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED),
        "new-tab command accepted a host without native tabs");
    ComPtr<IShellItemArray> selectedItems;
    ComPtr<IShellItem> selectedItem;
    DWORD selectedCount=0;
    PIDLIST_ABSOLUTE selectedId=nullptr;
    Check(SUCCEEDED(state.folderView->GetSelection(FALSE,&selectedItems)) && selectedItems &&
        SUCCEEDED(selectedItems->GetCount(&selectedCount)) && selectedCount==1 &&
        SUCCEEDED(selectedItems->GetItemAt(0,&selectedItem)) &&
        SUCCEEDED(SHGetIDListFromObject(selectedItem.Get(),&selectedId)),"native middle-click selection identity");
    PIDLIST_ABSOLUTE expected=nullptr;
    Check(SUCCEEDED(SHParseDisplayName((root/L"Folder").c_str(),nullptr,&expected,0,nullptr)),"private folder identity");
    Check(ILIsEqual(expected,selectedId),"new-tab command targeted another item");
    CoTaskMemFree(expected); CoTaskMemFree(selectedId);
    Check(GetCapture()!=state.hwnd && !state.mousePressActive && !state.marqueePossible,
        "folder tab activation retained capture or a pending mouse selection");
    g_middleClickNewTab=false; Open(state,folder,true); g_middleClickNewTab=true;
    Check(!TabActivationFixture::calls,"disabled option invoked a new-tab command");
    host.Navigate((root/L"Folder").wstring(),0);
    const auto empty=ContentViewport(state,true);
    Check(EmptySpaceAt(state,POINT{empty.left+1,empty.top+1}),"empty folder was not background");
    NavigateCurrentShellTab(state,SBSP_PARENT | SBSP_SAMEBROWSER);
    host.Bind(3);
    Check(!_wcsicmp(state.folderPath.c_str(),root.c_str()),"native parent navigation left the private hierarchy");
    // Actual USER capture callbacks clear interrupted selection. Delayed
    // message state cannot release a physically held button, or another HWND.
    state.mousePressActive=state.marqueePossible=true;
    SetCapture(state.hwnd); PointerFixture::held=true;
    ReleaseOrphanedMouseCapture(state,false);
    Check(GetCapture()==state.hwnd,"queued button-free state canceled a physically held gesture");
    PointerFixture::held=false; ReleaseOrphanedMouseCapture(state,true);
    Check(GetCapture()==state.hwnd,"held message state canceled a queued release");
    for(int button:{VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2}) {
        PointerFixture::otherHeld=button; ReleaseOrphanedMouseCapture(state,false);
        Check(GetCapture()==state.hwnd,"capture recovery canceled another held mouse button");
    }
    PointerFixture::otherHeld=0;
    ReleaseOrphanedMouseCapture(state,false);
    Check(GetCapture()!=state.hwnd && !state.mousePressActive && !state.marqueePossible,
        "orphaned capture retained selection or blocked the frame");
    SetCapture(state.nativeHwnd); ReleaseOrphanedMouseCapture(state,false);
    Check(GetCapture()==state.nativeHwnd,"capture recovery stole another control's gesture"); ReleaseCapture();
    // Exercise the existing asynchronous provider selection lane naturally.
    ProviderSelectionFixture::WriteZip(root/L"private.zip",ProviderSelectionFixture::ZipBytes({
        {"Folder/",""},{"Folder/inside.txt","private"},{"top.txt","private"}}));
    host.Navigate((root/L"private.zip").wstring(),2);
    Check(bool(state.namespaceIdentities),"archive did not exercise provider identities");
    folder=-1;
    for(int row=0;row<state.total;++row) if(state.entries[size_t(row)].folder) folder=row;
    Check(folder>=0,"native provider folder missing");
    g_doubleClickUp=false; g_twoFingerContextMenu=false;
    Open(state,folder,true); WaitForPreparedSelection(state);
    Check(!TabActivationFixture::calls && state.selected==folder && state.deferredSelectionKeys.empty(),
        "provider selection stalled, launched a window or depended on another gesture setting");
    std::cout<<"PASS: native two-finger recognition/retirement, shared right-click menus, independent toggles, folder/provider selection and tabless-host rejection, real capture recovery and empty-space geometry in Details/List/grid at 96/144 DPI and LTR/RTL\n";
}
