// Private filesystem PIDLs exercise Recycle Bin presentation without placing
// anything in the user's Recycle Bin. The resolver receives a provider's full
// display name while Windows still owns item binding and selection identities.
static void TestRecycleDisplayLabels(const std::wstring& fixture) {
    WorkerComApartment apartment;
    ModifierFixture::Session modifiers;
    AnimationClockFixture::Freeze clock;
    const auto root=std::filesystem::path(fixture)/L"recycle-labels";
    for(const auto* directory:{L"one",L"two"}) {
        std::filesystem::create_directories(root/directory);
        HANDLE file=CreateFileW((root/directory/L"duplicate.txt").c_str(),GENERIC_WRITE,0,nullptr,
            CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"private duplicate filename"); CloseHandle(file);
    }
    ComPtr<IShellItem> parent;
    ComPtr<IShellFolder> folder;
    Check(SUCCEEDED(SHCreateItemFromParsingName(root.c_str(),nullptr,IID_PPV_ARGS(&parent))) &&
        SUCCEEDED(parent->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&folder))),"private label parent");
    ViewState state;
    state.hwnd=MessageWindow(); state.nativeHwnd=MessageWindow(); state.frameHwnd=state.hwnd;
    state.background->hwnd=state.hwnd; state.folder=folder; state.folderPath=kRecycleBinLocation;
    state.loaded=state.visible=state.inScope=true;
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    FocusFixture::Session focus(state.hwnd,state.nativeHwnd);
    for(const auto* directory:{L"one",L"two"}) {
        const auto full=(root/directory/L"duplicate.txt").wstring();
        NativeRowIdentity identity; PIDLIST_ABSOLUTE absolute=nullptr; PITEMID_CHILD child=nullptr;
        std::wstring relative=std::wstring(directory)+L"\\duplicate.txt";
        Check(SUCCEEDED(SHParseDisplayName(full.c_str(),nullptr,&absolute,0,nullptr)) &&
            SUCCEEDED(folder->ParseDisplayName(nullptr,nullptr,relative.data(),nullptr,&child,nullptr)),
            "private duplicate Shell identity");
        identity.absolute.reset(absolute); identity.child.reset(child); identity.namespaceItem=true;
        identity.cachedName=full;
        for(bool recycle:{false,true}) {
            Entry entry; entry.name=full;
            Check(SUCCEEDED(ResolveNativeRowIdentity(identity,entry,folder.Get(),false,recycle)),"full path label resolution");
            Check(entry.name==full && entry.path==NamespaceItemKey(absolute) &&
                PidlBytesEqual(entry.shellIdentity.get(),absolute) && PidlBytesEqual(entry.shellChild.get(),child),
                "display shortening changed namespace identity or original name");
            Check(EntryLabel(entry)==(recycle ? L"duplicate.txt" : full),"basename rule escaped Recycle Bin or retained a path");
            if(recycle) state.entries.push_back(std::move(entry));
        }
        identity.cachedName=L"duplicate.txt";
        Entry alreadyShort;
        Check(SUCCEEDED(ResolveNativeRowIdentity(identity,alreadyShort,folder.Get(),false,true)) &&
            EntryLabel(alreadyShort)==L"duplicate.txt","already short Shell label changed");
    }
    state.total=int(state.entries.size()); state.selectedFlags.assign(state.total,0);
    auto* native=new SelectionFolderView; native->state=&state; native->folder=folder; state.folderView.Attach(native);
    Check(state.entries[0].path!=state.entries[1].path && EntryLabel(state.entries[0])==EntryLabel(state.entries[1]),
        "duplicate labels lost distinct identity");
    for(int row:{0,1,0}) {
        Select(state,row);
        Check(state.selected==row && native->selected.size()==1 &&
            PidlBytesEqual(native->selected[0].get(),state.entries[size_t(row)].shellChild.get()),
            "selection used the common display basename instead of the correct child PIDL");
    }
    Stop(state.background); UnregisterAnimationWindow(state.hwnd);
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0); DestroyWindow(state.nativeHwnd); DestroyWindow(state.hwnd);
    std::cout<<"PASS: Recycle Bin basename-only labels preserve duplicate Shell identities and native selection\n";
}

// Intentional read-only OS probe: resolve Gallery's namespace, never enumerate
// its images. Native fallback and return use a private ExplorerBrowser folder.
static void TestGalleryNativeScope(const std::wstring& fixture) {
    WorkerComApartment apartment;
    ModifierFixture::Session modifiers;
    AnimationClockFixture::Freeze clock;
    ResolveGalleryFolderIdentity();
    Check(bool(g_galleryFolderIdentity),"registered Gallery identity could not be resolved");
    PIDLIST_ABSOLUTE raw=nullptr;
    Check(SUCCEEDED(SHParseDisplayName(L"shell:::{e88865ea-0e1c-4e20-9aa6-edcd0212c87c}",nullptr,&raw,0,nullptr)) && raw,
        "independent Gallery namespace spelling");
    OwnedPidl gallery(raw);
    const auto destination=FolderLocation(gallery.get());
    Check(destination==kUnsupportedShellLocation && !IsSupportedLocation(destination),"Gallery accepted the GPU overlay");
    for(const auto* known:{&FOLDERID_ComputerFolder,&FOLDERID_RecycleBinFolder,&FOLDERID_AppsFolder}) {
        PIDLIST_ABSOLUTE other=nullptr;
        Check(SUCCEEDED(SHGetKnownFolderIDList(*known,0,nullptr,&other)),"neighbor namespace identity");
        OwnedPidl owned(other);
        Check(IsSupportedLocation(FolderLocation(owned.get())),"Gallery exclusion escaped its exact namespace");
    }
    const auto root=std::filesystem::path(fixture)/L"Gallery";
    std::filesystem::create_directory(root); // The name alone must never exclude an ordinary folder.
    ProviderSelectionFixture::Browser host(true);
    auto& state=host.state;
    FocusFixture::Session focus(host.frame,state.hwnd);
    host.Navigate(root.wstring(),0);
    const auto location=state.folderPath;
    auto view=state.folderView; auto folder=state.folder; auto browser=state.shellBrowser;
    Check(SuppressNativeContent(state,state.nativeHwnd) && !state.suppressedNativeHosts.empty(),"private native suppression");
    ReportShellNavigationEvent(state.hwnd,ShellNavigationSignal::Completed,L"Gallery scope",&destination,nullptr,{});
    Check(!state.inScope && state.suppressedNativeHosts.empty() && !PresentationVisible(state),
        "Gallery fallback left native suppression or a GPU surface visible");
    AttachView(state.nativeHwnd,view,folder,browser,destination);
    Check(!state.inScope && state.suppressedNativeHosts.empty(),"attachment ignored Gallery exclusion");
    AttachView(state.nativeHwnd,view,folder,browser,location);
    Check(state.inScope && state.visible && !state.suppressedNativeHosts.empty(),"return from native-only view failed to reattach");
    g_galleryFolderIdentity.reset();
    std::cout<<"PASS: exact Gallery scope, neighboring namespaces, native restoration and filesystem return\n";
}
