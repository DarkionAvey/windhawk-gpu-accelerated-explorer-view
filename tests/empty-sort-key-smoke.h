// Explorer can persist a sort column whose PROPERTYKEY is all zero (seen in a
// This PC > drive ShellBag) and DefView reports it unchanged. Reproduce that
// state through the real view's own setter on private files; Windows' readback
// is the oracle. Names must load in enumeration order and a later real sort
// must still be adopted.
static void TestEmptyShellSortKey(const std::wstring& fixture) {
    const auto root=std::filesystem::path(fixture)/L"empty-sort-key";
    std::filesystem::create_directory(root);
    for (const auto* name : {L"b.txt",L"c.txt",L"a.txt"}) {
        HANDLE file=CreateFileW((root/name).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"empty sort key private file");
        CloseHandle(file);
    }
    const bool previous=g_preSort.exchange(true);
    struct Restore { bool preSort; ~Restore() {g_preSort=preSort;} } restore{previous};
    {
    ProviderSelectionFixture::Browser host;
    auto& state=host.state;
    FocusFixture::Session focus(host.frame,state.hwnd);
    host.Navigate(root.wstring(),3);
    const auto settle=[&](const char* message) {
        Check(Eventually([&] {host.Pump(); ReceiveDirectoryResults(state);
            return state.loaded && !state.directoryPending;},30000),message);
        StopRowTransition(state);
    };
    settle("empty sort key baseline publication");
    SORTCOLUMN empty{};
    empty.direction=SORT_ASCENDING;
    const HRESULT set=state.folderView->SetSortColumns(&empty,1);
    host.Pump();
    int count=-1;
    SORTCOLUMN reported{};
    const bool readable=SUCCEEDED(state.folderView->GetSortColumnCount(&count)) && count>=1 &&
        SUCCEEDED(state.folderView->GetSortColumns(&reported,1));
    const bool windowsEmpty=readable && IsEqualGUID(reported.propkey.fmtid,GUID_NULL) && !reported.propkey.pid;
    std::wcout<<L"PROFILE: empty sort key set=0x"<<std::hex<<static_cast<ULONG>(set)<<std::dec
        <<L" count="<<count<<L" windowsEmpty="<<windowsEmpty<<std::endl;
    Check(windowsEmpty,"Windows DefView did not retain the empty sort key (test cannot reproduce Explorer's state)");
    SortSpec read;
    Check(ReadShellSort(state,read) && !read.explicitOrder && read.secondary.empty(),
        "an empty Shell sort key was treated as a read failure or invented an order");
    // Enter the folder through the real reset/cache path: the initial read
    // sees the empty key, and names must not wait for a sort that never comes.
    state.folderCache.folders.clear();
    auto folder=state.folder;
    ResetForNewDirectory(state,root.wstring(),folder,false,state.activeTabHost);
    Check(!state.nativeSortPending && !SortReadDefersNames(state) && !state.sort.explicitOrder,
        "an empty Shell sort key left the folder's names deferred");
    settle("an empty Shell sort key blocked directory publication");
    Check(state.total==3 && !state.displayedSort.explicitOrder,"empty sort key published the wrong snapshot");
    // A real later sort is adopted and re-sorts the published rows.
    SORTCOLUMN name{PKEY_ItemNameDisplay,SORT_DESCENDING};
    Check(SUCCEEDED(state.folderView->SetSortColumns(&name,1)),"native sort after empty key");
    host.Pump();
    RefreshSortFromExplorer(state,true,true);
    settle("native sort after an empty key was not published");
    Check(state.sort.explicitOrder && SameKey(state.sort.key,PKEY_ItemNameDisplay) &&
        state.sort.direction==SORT_DESCENDING && SameSort(state.sort,state.displayedSort) &&
        state.entries.front().name==L"c.txt","native sort after an empty key was not adopted");
    }
    Check(ReleasePrivateFixture(root),"empty sort key fixture cleanup did not settle");
    std::cout<<"PASS: an empty Shell sort key loads names in enumeration order and a later native sort is adopted\n";
}
