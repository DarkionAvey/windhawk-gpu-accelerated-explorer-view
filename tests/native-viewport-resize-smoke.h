// Real private Shell windows: resize through ExplorerBrowser, never fabricated
// WM_SIZE/WINDOWPOS messages or user Explorer windows.
static void TestNativeViewportResize(const std::wstring& fixture) {
    const auto root=std::filesystem::path(fixture)/L"native-resize";
    Check(std::filesystem::create_directory(root),"resize private folder");
    for(int i=0;i<3;++i) {
        HANDLE file=CreateFileW((root/(std::to_wstring(i)+L".txt")).c_str(),GENERIC_WRITE,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"resize private item"); CloseHandle(file);
    }
    ProviderSelectionFixture::Browser host;
    host.Navigate(root.wstring(),3);
    auto& state=host.state;
    for(const FOLDERVIEWMODE mode : {FVM_DETAILS,FVM_ICON,FVM_TILE}) {
        Check(SUCCEEDED(state.folderView->SetViewModeAndIconSize(mode,48)),"resize native view mode");
        host.Pump();
        Check(SuppressNativeContent(state),"resize native suppression");
        HWND child=nullptr;
        for(const auto& viewport : state.suppressedNativeViewports)
            if(viewport->parent==state.nativeHwnd) { child=viewport->hwnd; break; }
        Check(child!=nullptr,"resize direct Shell presentation child");
        RECT parent{}; GetClientRect(state.nativeHwnd,&parent);
        SetNativeInputLayout(state,true);
        RECT initial{}; GetWindowRect(child,&initial);
        MapWindowPoints(nullptr,state.nativeHwnd,reinterpret_cast<POINT*>(&initial),2);
        const LONG rightInset=parent.right-initial.right, bottomInset=parent.bottom-initial.bottom;
        SetNativeInputLayout(state,false);
        for(const bool expanded : {false,true}) {
            SetNativeInputLayout(state,expanded);
            for(RECT size : {RECT{0,0,1460,910},RECT{0,0,790,540},RECT{0,0,1230,780}}) {
                Check(SUCCEEDED(host.browser->SetRect(nullptr,size)),"resize browser rectangle");
                host.Pump();
                // Menu opening must repair even a previously expanded nonempty list.
                SetNativeInputLayout(state,true);
                GetClientRect(state.nativeHwnd,&parent);
                RECT actual{}; GetWindowRect(child,&actual);
                MapWindowPoints(nullptr,state.nativeHwnd,reinterpret_cast<POINT*>(&actual),2);
                Check(actual.left==initial.left && actual.top==initial.top &&
                    actual.right==parent.right-rightInset && actual.bottom==parent.bottom-bottomInset,
                    "hidden native viewport retained an old size after browser resize");
                Check(!IsWindowVisible(child),"native resize exposed hidden presentation");
                if(!expanded) {
                    SetNativeInputLayout(state,false);
                    GetClientRect(child,&actual);
                    Check(actual.right==0 && actual.bottom==0,"resize failed to collapse native presentation");
                }
            }
        }
        SetNativeInputLayout(state,false);
    }
    RECT resized{0,0,1370,850};
    Check(SUCCEEDED(host.browser->SetRect(nullptr,resized)),"resize before native restoration");
    host.Pump();
    const HWND child=state.suppressedNativeViewports.front()->hwnd;
    const HWND parent=GetParent(child);
    RestoreNativeContent(state);
    RECT native{},actual{}; GetClientRect(parent,&native); GetClientRect(child,&actual);
    Check(actual.right==native.right && actual.bottom==native.bottom,
        "native restoration retained a cropped viewport");
    std::cout<<"PASS: real hidden Shell view resizing, repeated menu layout and restoration in Details/Icon/Tiles\n";
}
