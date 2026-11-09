// Exercise actual attachment and exit routes against a private ExplorerBrowser.
// Item count alone cannot prove that Explorer's presentation returned.
namespace NativeReturnFixture {
inline void Fonts() {
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"native return fonts");
}
inline std::wstring Folder(const std::wstring& fixture) {
    auto root=std::filesystem::path(fixture)/L"native-return";
    std::filesystem::create_directories(root);
    for(int i=0;i<5;++i) std::ofstream(root/(L"visible-"+std::to_wstring(i)+L".txt"))<<"private native return";
    return root.wstring();
}
inline void MessageBarrier(OptionEffects::LiveExplorer& live) {
    // Exercise the real fallback installed when USER rejects the DLL WinEvent
    // subscription. Keep the production registry and normal teardown owner.
    const HHOOK messages=SetWindowsHookExW(WH_CALLWNDPROC,NativeSurfaceMessages,nullptr,GetCurrentThreadId());
    Check(messages!=nullptr,"native return message barrier installation");
    AcquireSRWLockExclusive(&g_shellHookLock);
    auto& hooks=g_shellViewHooks.at(live.frame);
    const auto previousMessages=std::exchange(hooks.presentationMessages,messages);
    const auto previousEvents=std::exchange(hooks.presentationHandle,nullptr);
    ReleaseSRWLockExclusive(&g_shellHookLock);
    if(previousMessages) UnhookWindowsHookEx(previousMessages);
    if(previousEvents) UnhookWinEvent(previousEvents);
}
inline void RequestNativeShow(OptionEffects::LiveExplorer& live,bool deferred) {
    const HWND host=FolderContentHost(live.native);
    constexpr UINT flags=SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW;
    if(deferred) {
        HDWP batch=BeginDeferWindowPos(1);
        Check(batch!=nullptr,"native return deferred batch");
        batch=NativeDeferWindowPosHook(batch,host,nullptr,0,0,0,0,flags);
        Check(batch && EndDeferWindowPos(batch),"native return deferred show");
    } else Check(NativeSetWindowPosHook(host,nullptr,0,0,0,0,flags),"native return immediate show");
    Check(!::IsWindowVisible(host),"native show escaped GPU suppression");
    Check(GetPropW(host,kNativeVisibilityIntentProperty)==reinterpret_cast<HANDLE>(1),
        "message barrier overwrote native show intent with the mod's suppression hide");
}
inline void CheckNative(OptionEffects::LiveExplorer& live) {
    Check(!Regression::OnInputDesktop(),"native presentation probe requires its private desktop");
    MONITORINFO monitor{sizeof(monitor)};
    Check(GetMonitorInfoW(MonitorFromWindow(live.frame,MONITOR_DEFAULTTOPRIMARY),&monitor),"native return monitor");
    SetWindowPos(live.frame,nullptr,monitor.rcWork.left,monitor.rcWork.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    live.Pump();
    Check(!::IsWindowVisible(live.overlay),"GPU list remained visible beside native view");
    Check(::IsWindowVisible(live.native),"native Shell view remained hidden after return");
    int count=0;
    Check(SUCCEEDED(live.folderView->ItemCount(SVGIO_ALLVIEW,&count)) && count==5,"native return lost its file model");
    unsigned lists=0;
    EnumChildWindows(live.native,[](HWND child,LPARAM value)->BOOL {
        wchar_t name[64]{}; GetClassNameW(child,name,ARRAYSIZE(name));
        if(wcscmp(name,L"DirectUIHWND") && wcscmp(name,L"SysListView32")) return TRUE;
        RECT rect{}; GetClientRect(child,&rect);
        Check(::IsWindowVisible(child) && rect.right>0 && rect.bottom>0 && !GetPropW(child,kNativeViewportProperty),
            "native return left a hidden/collapsed presentation child");
        ++*reinterpret_cast<unsigned*>(value); UpdateWindow(child);
        return TRUE;
    },reinterpret_cast<LPARAM>(&lists));
    Check(lists!=0,"native return has no presentation child");
    // Ask the actual provider for visible row geometry on an MTA while its
    // owner STA pumps. A full COM model and nonempty HWNDs alone are insufficient.
    std::atomic<bool> done=false; HRESULT result=E_PENDING; unsigned realized=0;
    const HWND native=live.native;
    std::thread client([&] {
        const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        ComPtr<IUIAutomation> automation; ComPtr<IUIAutomationElement> root;
        ComPtr<IUIAutomationCondition> condition; ComPtr<IUIAutomationElementArray> rows;
        result=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation));
        VARIANT kind{}; kind.vt=VT_I4; kind.lVal=UIA_ListItemControlTypeId;
        if(SUCCEEDED(result)) result=automation->ElementFromHandle(native,&root);
        if(SUCCEEDED(result)) result=automation->CreatePropertyCondition(UIA_ControlTypePropertyId,kind,&condition);
        if(SUCCEEDED(result)) result=root->FindAll(TreeScope_Descendants,condition.Get(),&rows);
        int count=0; if(rows) rows->get_Length(&count);
        for(int i=0;i<count;++i) {
            ComPtr<IUIAutomationElement> row; RECT rect{}; BSTR name=nullptr;
            if(SUCCEEDED(rows->GetElement(i,&row)) && SUCCEEDED(row->get_CurrentName(&name)) &&
                name && std::wstring_view(name).starts_with(L"visible-") &&
                SUCCEEDED(row->get_CurrentBoundingRectangle(&rect)) && rect.right>rect.left && rect.bottom>rect.top) ++realized;
            SysFreeString(name);
        }
        rows.Reset(); condition.Reset(); root.Reset(); automation.Reset();
        if(SUCCEEDED(initialized)) CoUninitialize(); done.store(true,std::memory_order_release);
    });
    const bool completed=Eventually([&] {live.Pump(); return done.load(std::memory_order_acquire);},30000);
    client.join();
    std::cout<<"Native rows hr=0x"<<std::hex<<unsigned(result)<<std::dec<<" realized="<<realized<<'\n';
    Check(completed && SUCCEEDED(result) && realized==5,"native return has a model but no realized visible file rows");
}
inline void Button(OptionEffects::LiveExplorer& live) {
    Check(IsWindow(live.state->viewToggle) && ::IsWindowVisible(live.state->viewToggle),"toggle button disappeared");
    // Let the genuine BUTTON produce its parent notification.
    SendMessageW(live.state->viewToggle,BM_CLICK,0,0); live.Pump();
}
}
static void TestNativeReturnRoutesCore(const std::wstring& fixture) {
    NativeReturnFixture::Fonts();
    FocusFixture::Session focus(nullptr,nullptr);
    ModifierFixture::Session modifiers;
    const bool button=g_showNativeViewToggleButton.exchange(true);
    const auto folder=NativeReturnFixture::Folder(fixture);
    {
        OptionEffects::LiveExplorer live(folder);
        NativeReturnFixture::MessageBarrier(live);
        FocusFixture::owner=FocusFixture::foreground=live.frame; FocusFixture::focus=live.native;
        RefreshViewToggleForFrame(live.frame);
        for(const auto mode:{FVM_DETAILS,FVM_LIST,FVM_ICON}) {
            Regression::Scope scope("native return mode="+std::to_string(mode));
            live.SetMode(mode,48);
            RECT client{}; GetClientRect(live.frame,&client);
            client.right-=80; client.bottom-=40;
            Check(SUCCEEDED(live.browser->SetRect(nullptr,client)),"native browser resize while GPU owns the viewport");
            live.Pump();
            NativeReturnFixture::RequestNativeShow(live,mode==FVM_ICON);
            NativeReturnFixture::Button(live);
            Check(!live.state->visible,"button did not choose native view");
            NativeReturnFixture::CheckNative(live);
            NativeReturnFixture::Button(live);
            Check(live.state->visible,"button could not return to GPU view");
            live.Pump();
        }
        const auto dispatch=g_dispatchOriginal; g_dispatchOriginal=::DispatchMessageW;
        modifiers.keys[VK_CONTROL]=modifiers.keys[VK_MENU]=0x80;
        MSG key{}; key.hwnd=live.frame; key.message=WM_KEYDOWN; key.wParam=VK_F9;
        NativeReturnFixture::RequestNativeShow(live,true);
        DispatchHook(&key); live.Pump();
        Check(!live.state->visible,"Ctrl+Alt+F9 did not choose native view");
        NativeReturnFixture::CheckNative(live);
        Check(::IsWindowVisible(live.state->viewToggle) && IsWindowEnabled(live.state->viewToggle),
            "native view lost the enabled switch button");
        g_showNativeViewToggleButton=false; RefreshViewToggleForFrame(live.frame);
        Check(!::IsWindowVisible(live.state->viewToggle),"button ignored its explicit hide option");
        g_showNativeViewToggleButton=true; RefreshViewToggleForFrame(live.frame);
        Check(::IsWindowVisible(live.state->viewToggle),"button could not return during native view");
        DispatchHook(&key); live.Pump();
        modifiers.keys[VK_CONTROL]=modifiers.keys[VK_MENU]=0;
        g_dispatchOriginal=dispatch;
        Check(live.state->visible,"Ctrl+Alt+F9 did not restore GPU mode");
        // Content is a GPU layout: Explorer's own mode change keeps the GPU
        // list and the native list hidden.
        {
            NativeViewInput input(live.state,true);
            Check(SUCCEEDED(live.folderView->SetViewModeAndIconSize(FVM_CONTENT,48)),"Content view mode");
        }
        Check(Eventually([&] {live.Pump(); return live.state->layoutNativeMode==FVM_CONTENT;}),
            "Content view did not reach the GPU layout");
        Check(live.state->detailsViewActive && ::IsWindowVisible(live.overlay) && !::IsWindowVisible(live.native),
            "Content view conceded presentation to native Explorer");
    }
    g_showNativeViewToggleButton=button;
}
static void TestNativeReturnUnloadCore(const std::wstring& fixture) {
    NativeReturnFixture::Fonts();
    FocusFixture::Session focus(nullptr,nullptr);
    const auto folder=NativeReturnFixture::Folder(fixture);
    OptionEffects::LiveExplorer live(folder);
    NativeReturnFixture::MessageBarrier(live);
    NativeReturnFixture::RequestNativeShow(live,false);
    Wh_ModBeforeUninit(); Wh_ModUninit();
    Check(!IsWindow(live.overlay),"actual unload retained the GPU overlay");
    NativeReturnFixture::CheckNative(live);
    Check(g_tabOverlaysByFrame.empty() && !g_instances.load(),"actual unload retained tab ownership");
}
static void TestNativeReturnRoutes(const std::wstring&) {
    PrivateDesktopFixture::Run(L"native-return",L"--test native-return-routes-core",120000);
}
static void TestNativeReturnUnload(const std::wstring&) {
    PrivateDesktopFixture::Run(L"native-unload",L"--test native-return-unload-core",120000);
}


static void TestTypeSearchInput(const std::wstring& fixture) {
    NativeReturnFixture::Fonts();
    FocusFixture::Session focus(nullptr,nullptr);
    ModifierFixture::Session modifiers;
    const auto root=std::filesystem::path(fixture)/L"type-search-input";
    std::filesystem::create_directories(root);
    for(const auto* name:{L"Program files.txt",L"Folder (1) & [x].txt",L"Zebra.txt"}) std::ofstream(root/name)<<"private";
    OptionEffects::LiveExplorer live(root.wstring());
    FocusFixture::owner=FocusFixture::foreground=live.frame; FocusFixture::focus=live.native;
    auto& state=*live.state;
    Check(!g_systemRelativeTime.attempted.load(),"Unicode ICU loaded before first search/tooltip demand");
    const auto original=g_dispatchOriginal; g_dispatchOriginal=::DispatchMessageW;
    const auto type=[&](const wchar_t* query) {
        ClearTypeSearch(state);
        for(const wchar_t* p=query;*p;++p) TypeToSelect(state,*p);
        Check(Eventually([&] {live.Pump(); return !state.typeSearchWaiting;}),
            "worker Unicode initialization did not resume the active query");
    };
    type(L"Program");
    MSG space{}; space.hwnd=live.native; space.message=WM_KEYDOWN; space.wParam=VK_SPACE;
    space.lParam=1|(LPARAM(MapVirtualKeyW(VK_SPACE,MAPVK_VK_TO_VSC))<<16);
    Check(OverlayKeyboardTarget(space)==live.overlay,"search Space key escaped to native preview");
    ModifierFixture::Translate(&space);
    MSG character{};
    Check(PeekMessageW(&character,live.native,WM_CHAR,WM_CHAR,PM_REMOVE) && character.wParam==L' ',"Windows did not translate Space");
    Check(OverlayKeyboardTarget(character)==live.overlay,"translated search Space escaped to the Shell");
    DispatchHook(&character); TypeToSelect(state,L'f'); TypeToSelect(state,L'i');
    Check(state.typeSearch==L"Program fi" && state.entries[size_t(state.selected)].name==L"Program files.txt","Space broke incremental search");
    MSG back=space; back.wParam=VK_BACK;
    back.lParam=1|(LPARAM(MapVirtualKeyW(VK_BACK,MAPVK_VK_TO_VSC))<<16);
    Check(OverlayKeyboardTarget(back)==live.overlay,"active-search Backspace escaped to Shell navigation");
    ModifierFixture::Translate(&back);
    DispatchHook(&back);
    Check(state.typeSearch==L"Program f","Backspace did not delete exactly one search character");
    Check(PeekMessageW(&character,live.native,WM_CHAR,WM_CHAR,PM_REMOVE) && character.wParam==L'\b',"Windows did not translate Backspace");
    DispatchHook(&character);
    Check(state.typeSearch==L"Program f","translated Backspace deleted a second character");
    TypeToSelect(state,L'i');
    Check(state.entries[size_t(state.selected)].name==L"Program files.txt","search did not continue after correcting a typo");
    for(const auto* query:{L"programfil",L"Program fi",L"folder(1)&["}) {
        type(query);
        const auto expected=query[0]==L'f' ? L"Folder (1) & [x].txt":L"Program files.txt";
        Check(state.entries[size_t(state.selected)].name==expected,"search whitespace/punctuation selected the wrong item");
    }
    // The actual USER timer is the timeout authority. No manufactured timer.
    Check(Eventually([&] {live.Pump(); return state.typeSearch.empty();}),"native type-search timeout did not clear the query");
    Check(!OverlayKeyboardTarget(space),"idle Space no longer belongs to native preview extensions");
    Check(!OverlayKeyboardTarget(back),"idle Backspace no longer belongs to native navigation");
    TypeToSelect(state,L'A');
    for(wchar_t ch:std::wstring(L"\U0001F600")) TypeToSelect(state,ch);
    TypeToSelect(state,L'\b');
    Check(state.typeSearch==L"A","search Backspace left an orphaned emoji surrogate");
    ClearTypeSearch(state);
    g_dispatchOriginal=original;
    std::cout<<"PASS: Windows-translated Space routing, incremental whitespace/punctuation matching and real timeout\n";
}
