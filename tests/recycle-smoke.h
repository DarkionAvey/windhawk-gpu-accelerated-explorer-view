// Duplicate display names are legitimate namespace items. Use only private
// filesystem fixtures for identity/property assertions; never recycle, restore
// or delete anything from the user's real Recycle Bin.
static void TestRecycleIdentities(HWND frame, const std::wstring& fixture) {
    const auto first=std::filesystem::path(fixture)/L"recycle-a";
    const auto second=std::filesystem::path(fixture)/L"recycle-b";
    std::filesystem::create_directory(first); std::filesystem::create_directory(second);
    std::vector<OwnedPidl> identities;
    for (const auto& directory : {first,second}) {
        const auto file=directory/L"duplicate.bin";
        HANDLE handle=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,
            CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        DWORD written=0; const char bytes[]="private fixture";
        const DWORD size=directory==first ? 1 : sizeof(bytes);
        Check(handle!=INVALID_HANDLE_VALUE && WriteFile(handle,bytes,size,&written,nullptr),
            "namespace duplicate fixture");
        CloseHandle(handle);
        PIDLIST_ABSOLUTE pidl=nullptr;
        Check(SUCCEEDED(SHParseDisplayName(file.c_str(),nullptr,&pidl,0,nullptr)) && pidl,
            "namespace duplicate PIDL");
        identities.emplace_back(pidl);
    }
    ViewState state;
    state.hwnd=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,640,480,
        frame,nullptr,GetModuleHandleW(nullptr),nullptr);
    state.frameHwnd=frame; state.background->hwnd=state.hwnd;
    state.visible=state.inScope=true; state.folderPath=kRecycleBinLocation;
    state.directoryToken=g_nextToken.fetch_add(1);
    state.directoryRequestedAt=GetTickCount64();
    state.background->directoryGeneration=state.directoryToken;
    UpdateMetrics(state);
    NativeRowsJob job; job.token=state.directoryToken;
    job.presentation=std::make_unique<DirectoryJob>();
    job.presentation->hwnd=state.hwnd; job.presentation->path=kRecycleBinLocation;
    job.presentation->token=job.token; job.presentation->allowInitialPreview=true;
    for (const auto& pidl : identities) {
        NativeRowIdentity identity;
        identity.absolute.reset(ILCloneFull(pidl.get()));
        identity.child.reset(ILClone(ILFindLastID(pidl.get())));
        identity.namespaceItem=true; job.identities.push_back(std::move(identity));
    }
    ResolveNativeRowsJob(*state.background,job,[](const NativeRowIdentity& identity, Entry& entry) {
        return ResolveNativeRowIdentity(identity,entry);
    });
    Check(ReceiveDirectoryResults(state) && state.loaded && state.total==2 &&
        !state.entries[0].folder && !state.entries[1].folder &&
        state.entries[0].name==state.entries[1].name &&
        state.entries[0].path!=state.entries[1].path && state.entryPathIndex.size()==2,
        "namespace publication conflated duplicate names, lost attributes, or bypassed directory commit");
    for (int i=0;i<2;++i) {
        Check(GpuRowForNativePath(state,NamespaceItemKey(identities[i].get()))==i,
            "namespace identity index selected the wrong duplicate");
        ComPtr<IPropertyStore> store;
        Check(SUCCEEDED(EntryPropertyStore(state.entries[i].path,state.entries[i].shellIdentity.get(),
            GPS_DEFAULT,store.GetAddressOf())) && store,"namespace property store lost its PIDL");
        PROPVARIANT value{}; PropVariantInit(&value);
        Check(SUCCEEDED(store->GetValue(PKEY_Size,&value)) && value.vt==VT_UI8 &&
            value.uhVal.QuadPart==static_cast<ULONGLONG>(i ? sizeof("private fixture") : 1),
            "namespace metadata was fetched using a display name/opaque cache key");
        PropVariantClear(&value);
    }
    Entry hiddenExtension=state.entries.front(); hiddenExtension.name=L"duplicate";
    std::wstring iconPath; bool attributes=false;
    Check(IconKey(state,hiddenExtension,iconPath,attributes)==hiddenExtension.path && !attributes,
        "namespace icons relied on a display name having a file extension");
    auto sorted=state.entries;
    SortSpec size=DefaultSort(); size.explicitOrder=true; size.kind=SortKind::Size;
    size.key=PKEY_Size; size.direction=SORT_DESCENDING;
    std::map<std::wstring,std::wstring> types;
    SortDirectoryEntries(sorted,size,types,*state.background,state.directoryToken);
    Check(sorted.front().path==NamespaceItemKey(identities[1].get()) &&
        SameEntryMetadata(sorted.front(),state.entries[1]),
        "namespace Size sort used zero filesystem hints or invalidated immutable cached properties");
    ObserveDirectoryWatch(state,GetTickCount64(),true);
    Check(state.watchedChangePending && state.watchedChangeFirstAt,
        "namespace Shell notification did not enter the shared batch refresh");
    auto cached=state.entries[0]; cached.propertyEpoch=state.propertyEpoch;
    cached.cells={{PKEY_Size,L"cached size"}};
    CacheRowDetails(state,cached,true);
    auto restored=cached; restored.cells.clear(); restored.size.clear();
    CacheRowDetails(state,restored,false);
    Check(restored.cells.size()==1 && restored.cells[0].text==L"cached size",
        "Recycle Bin metadata cache discarded the name/PIDL identity required for validation");
    // The real namespace enumerator must bind its root once, then reuse that
    // parent for every child. Repeated root hints must not resolve names or
    // publish replacement snapshots when membership has not changed.
    TabBackground checks; checks.hwnd=state.hwnd; checks.directoryGeneration=987654;
    PIDLIST_ABSOLUTE parent=nullptr;
    Check(SUCCEEDED(SHParseDisplayName(first.c_str(),nullptr,&parent,0,nullptr)),"namespace check root");
    OwnedPidl parentIdentity(parent);
    auto enumerate = [&](std::shared_ptr<const NamespaceIdentities> expected = {}) {
        NativeRowsJob request; request.token=checks.directoryGeneration.load();
        request.folder.reset(ILCloneFull(parentIdentity.get()));
        request.presentation=std::make_unique<DirectoryJob>();
        request.presentation->hwnd=state.hwnd; request.presentation->token=request.token;
        request.presentation->path=kRecycleBinLocation;
        request.presentation->namespaceFolder=SharedPidl(ILCloneFull(parentIdentity.get()),TaskMemDeleter{});
        request.presentation->verifyIdentities=std::move(expected);
        NamespaceFixture::countBindings=true; NamespaceFixture::bindings=0;
        EnumerateNativeRowsJob(checks,request);
        NamespaceFixture::countBindings=false;
        Check(NamespaceFixture::bindings==1,"namespace enumerator rebound the folder for each child");
        Check(checks.directoryResults.size()==1,"namespace check published intermediate/replacement rows");
        auto result=std::move(checks.directoryResults.front()); checks.directoryResults.pop_front();
        return result;
    };
    auto snapshot=enumerate();
    Check(snapshot.kind==DirectoryResult::Complete && snapshot.entries.size()==1 && snapshot.namespaceIdentities,
        "namespace identity snapshot was not built on the names worker");
    for (int i=0;i<16;++i) {
        auto result=enumerate(snapshot.namespaceIdentities);
        Check(result.kind==DirectoryResult::NamespaceCheck && !result.namespaceChanged && result.entries.empty(),
            "unchanged root notification rebuilt names");
    }
    const auto added=first/L"added.txt";
    HANDLE extra=CreateFileW(added.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);
    Check(extra!=INVALID_HANDLE_VALUE,"namespace membership fixture"); CloseHandle(extra);
    Check(enumerate(snapshot.namespaceIdentities).namespaceChanged,"root check missed insertion");
    auto larger=enumerate(); std::filesystem::remove(added);
    Check(enumerate(larger.namespaceIdentities).namespaceChanged,"root check missed deletion");
    // Check scheduling keeps directory/detail generations and visible cells
    // intact. Several pending hints share one raw-identity request.
    state.directoryFolderIdentity=SharedPidl(ILCloneFull(parentIdentity.get()),TaskMemDeleter{});
    state.namespaceIdentities=snapshot.namespaceIdentities;
    state.watchedChangePending=state.watchedChangeVerifyOnly=false;
    const auto generation=state.directoryToken, epoch=state.columnEpoch, icons=state.iconToken;
    for (int i=0;i<16;++i) ObserveDirectoryWatch(state,GetTickCount64(),true,true);
    UpdateWatchedDirectory(state,GetTickCount64());
    Check(state.namespaceCheckPending && !state.directoryPending && state.directoryToken==generation &&
        state.columnEpoch==epoch && state.iconToken==icons,"root hint cancelled cold viewport jobs");
    Check(Eventually([&] {
        ExclusiveSrwLock lock(state.background->directoryLock);
        return !state.background->directoryResults.empty();
    }),"namespace check did not finish");
    ReceiveDirectoryResults(state);
    Check(!state.namespaceCheckPending && !state.watchedChangePending && !state.directoryPending,
        "unchanged namespace result scheduled a full refresh");
    state.shellItemOverflow=true; state.shellVisualChangeAt=0;
    FlushShellChangeHints(state,GetTickCount64()+kShellVisualCoalesceMs);
    Check(!state.watchedChangePending && !state.directoryPending && state.directoryToken==generation,
        "metadata overflow was promoted to a names refresh");
    CancelRename(state); Stop(state.background); DestroyWindow(state.hwnd);
    std::filesystem::remove(first/L"duplicate.bin"); std::filesystem::remove(first);
    std::filesystem::remove(second/L"duplicate.bin"); std::filesystem::remove(second);
    std::cout << "PASS: Recycle Bin duplicate identities/properties, one parent binding, unchanged root checks, insertion/deletion detection and no metadata-overflow refresh loop\n";
}
