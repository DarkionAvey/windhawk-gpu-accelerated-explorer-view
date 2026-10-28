// Explicit read-only OS/Shell integration probe, excluded from the default
// private-fixture suite. Does not select, navigate, focus, or invoke commands.
static void TestProfileIdentityProbe() {
    PWSTR rawProfile=nullptr;
    Check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile,0,nullptr,&rawProfile)), "profile discovery");
    const std::wstring profile=rawProfile; CoTaskMemFree(rawProfile);
    ComPtr<IShellWindows> windows;
    Check(SUCCEEDED(CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_ALL,IID_PPV_ARGS(&windows))),
        "read-only Explorer discovery");
    long count=0; windows->get_Count(&count);
    unsigned inspected=0;
    for(long i=0;i<count;++i) {
        VARIANT index{}; index.vt=VT_I4; index.lVal=i;
        ComPtr<IDispatch> dispatch; ComPtr<IServiceProvider> services;
        ComPtr<IShellBrowser> browser; ComPtr<IShellView> view; ComPtr<IFolderView2> native;
        windows->Item(index,&dispatch);
        if(dispatch) dispatch.As(&services);
        if(services) services->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser));
        if(browser) browser->QueryActiveShellView(&view);
        if(view) view.As(&native);
        std::wstring location; ComPtr<IShellFolder> folder;
        if(!GetFolderPath(native.Get(),location,folder) || _wcsicmp(location.c_str(),profile.c_str())) continue;
        ComPtr<IPersist> persist; CLSID nativeClass{};
        Check(SUCCEEDED(folder.As(&persist)) && SUCCEEDED(persist->GetClassID(&nativeClass)), "native provider class");
        ComPtr<IShellFolder> reparsed; ComPtr<IPersist> reparsedPersist; CLSID reparsedClass{};
        Check(SUCCEEDED(BindDirectoryProvider(location,reparsed)) && SUCCEEDED(reparsed.As(&reparsedPersist)) &&
            SUCCEEDED(reparsedPersist->GetClassID(&reparsedClass)), "path-parsed provider class");
        wchar_t nativeId[40]{},reparsedId[40]{};
        StringFromGUID2(nativeClass,nativeId,40); StringFromGUID2(reparsedClass,reparsedId,40);
        std::wcout << L"PROFILE PROVIDERS native=" << nativeId << L" reparsed=" << reparsedId << std::endl;
        ViewState state;
        state.hwnd=IconTestWindow(); state.background->hwnd=state.hwnd;
        state.folderPath=location; state.folder=folder; state.inScope=true;
        QueueDirectoryRefresh(state);
        auto result=WaitForComplete(*state.background,state.directoryToken);
        const bool filesystem=IsEqualCLSID(nativeClass,CLSID_ShellFSFolder);
        const bool matches= !filesystem || (!result.namespaceIdentities &&
            std::none_of(result.entries.begin(),result.entries.end(),[](const Entry& e){return bool(e.shellChild);}));
        std::cout << "PROFILE IDENTITY native-filesystem=" << filesystem
            << " model-provider=" << bool(result.namespaceIdentities)
            << " rows=" << result.entries.size() << " native-root-retained="
            << bool(state.directoryFolderIdentity) << std::endl;
        Stop(state.background); DestroyWindow(state.hwnd);
        Check(result.error==ERROR_SUCCESS && matches,
            "names worker rebound the active filesystem profile as a different provider");
        Check(bool(state.directoryFolderIdentity), "names worker discarded the native folder binding");
        ++inspected;
    }
    Check(inspected>0,"no active Explorer profile view was available for the read-only probe");
    std::cout << "PASS: names worker preserves the live profile provider; Explorer selection and navigation untouched\n";
}
