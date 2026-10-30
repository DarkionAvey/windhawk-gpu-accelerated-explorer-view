// Public Shell sorting on private files. Native indices provide an independent
// ordering oracle; no invented notifications or user folders/focus are involved.
static void TestInitialSorting(const std::wstring& fixture) {
    const auto root=std::filesystem::path(fixture)/L"initial-sort";
    std::filesystem::create_directory(root);
    std::filesystem::create_directory(root/L"Folder");
    for (int i : {10,2,21,1,20,3}) {
        const auto path=root/(L"Item-"+std::to_wstring(i)+L".txt");
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE|FILE_WRITE_ATTRIBUTES,0,nullptr,CREATE_NEW,0,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"initial sort private file");
        const char bytes[32]{}; DWORD written=0;
        const ULONGLONG value=135000000000000000ULL+i*10000000ULL;
        const FILETIME stamp{DWORD(value),DWORD(value>>32)};
        Check(WriteFile(file,bytes,i,&written,nullptr) && SetFileTime(file,&stamp,&stamp,&stamp),"initial sort metadata");
        CloseHandle(file);
    }
    const bool previous=g_preSort.exchange(true);
    const auto placement=g_folderPlacement.exchange(FolderPlacement::ExplorerDefault);
    struct Restore { bool preSort; FolderPlacement placement;
        ~Restore() {g_preSort=preSort; g_folderPlacement=placement;}
    } restore{previous,placement};
    {
    ProviderSelectionFixture::Browser host;
    auto& state=host.state;
    FocusFixture::Session focus(host.frame,state.hwnd);
    host.Navigate(root.wstring(),7);
    Check(state.preSortEnabled && state.sortInitialized && state.trackNativeSort &&
        state.sort.explicitOrder && SameSort(state.sort,state.displayedSort),"first folder ignored native sorting");
    const auto settle=[&] {
        Check(Eventually([&] {host.Pump(); ReceiveDirectoryResults(state);
            return state.loaded && !state.directoryPending;},30000),"initial sort publication");
        StopRowTransition(state);
    };
    const auto nativeOrder=[&] {
        std::vector<std::wstring> paths;
        for (int i=0;i<state.total;++i) {
            PITEMID_CHILD raw=nullptr;
            Check(SUCCEEDED(state.folderView->Item(i,&raw)) && raw,"native sort row");
            OwnedPidl child(raw); ComPtr<IShellItem> item; PWSTR path=nullptr;
            Check(SUCCEEDED(SHCreateItemWithParent(nullptr,state.folder.Get(),child.get(),IID_PPV_ARGS(&item))) &&
                SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path)),"native sort row path");
            paths.emplace_back(path); CoTaskMemFree(path);
        }
        return paths;
    };
    // Turning off pre-sorting also rejects an automatically sorted cache,
    // rather than silently preserving the former default on the next visit.
    g_preSort=false;
    auto folder=state.folder;
    ResetForNewDirectory(state,root.wstring(),folder,false,state.activeTabHost); settle();
    Check(!state.sort.explicitOrder && !state.displayedSort.explicitOrder,"opt-out reused an automatic pre-sorted cache");
    g_preSort=true;
    ResetForNewDirectory(state,root.wstring(),folder,false,state.activeTabHost); settle();
    Check(state.sort.explicitOrder && SameSort(state.sort,state.displayedSort),"enabling pre-sorting reused an unsorted cache");
    for (const auto& key : {PKEY_ItemNameDisplay,PKEY_Size,PKEY_DateModified})
        for (const auto direction : {SORT_ASCENDING,SORT_DESCENDING}) {
            SORTCOLUMN column{key,direction};
            Check(SUCCEEDED(state.folderView->SetSortColumns(&column,1)),"native saved sort choice");
            host.Pump();
            // Re-enter through the real reset/cache path, without a GPU click.
            auto folder=state.folder;
            ResetForNewDirectory(state,root.wstring(),folder,false,state.activeTabHost);
            settle();
            const auto paths=nativeOrder();
            for (int i=0;i<state.total;++i) {
                if (_wcsicmp(paths[i].c_str(),state.entries[i].path.c_str()))
                    std::wcout<<L"SORT MISMATCH key="<<key.pid<<L" direction="<<int(direction)
                        <<L" row="<<i<<L" native="<<paths[i]<<L" GPU="<<state.entries[i].path<<std::endl;
                Check(_wcsicmp(paths[i].c_str(),state.entries[i].path.c_str())==0,"saved sort differs from native view order");
            }
        }
    // Native changes made while the overlay is off must replace its cache.
    SetActive(state.hwnd,false);
    SORTCOLUMN native{PKEY_Size,SORT_ASCENDING};
    Check(SUCCEEDED(state.folderView->SetSortColumns(&native,1)),"native-only sort choice"); host.Pump();
    SetActive(state.hwnd,true); settle();
    Check(SameKey(state.sort.key,PKEY_Size) && state.sort.direction==SORT_ASCENDING &&
        SameSort(state.sort,state.displayedSort),"native-to-overlay switch retained the old sort");
    // Read real native columns before invoking the same header path as input.
    state.deferColumnProviders=false; state.featureInitializationStep=std::max(3u,state.featureInitializationStep);
    FinishColumnTransition(state); RefreshExplorerColumns(state,true);
    Check(!state.columns.empty(),"native header columns");
    SortByHeader(state,0); settle();
    SortSpec updated;
    Check(ReadShellSort(state,updated) && SameSort(updated,state.sort) &&
        SameKey(updated.key,state.columns.front().key),"overlay header did not update native sorting");
    const SORTCOLUMN multiple[]={{PKEY_ItemTypeText,SORT_ASCENDING},{PKEY_DateModified,SORT_DESCENDING}};
    Check(SUCCEEDED(state.folderView->SetSortColumns(multiple,2)),"native secondary sort choice"); host.Pump();
    RefreshSortFromExplorer(state,true,true); settle();
    Check(state.sort.secondary.size()==1 && SameSort(state.sort,state.displayedSort),"secondary native key was dropped");
    const auto paths=nativeOrder();
    for (int i=0;i<state.total;++i)
        Check(_wcsicmp(paths[i].c_str(),state.entries[i].path.c_str())==0,"secondary sort differs from native order");
    g_preSort=false;
    state.folderCache.folders.clear();
    InitializeFolderSort(state,root.wstring());
    Check(!state.sort.explicitOrder && !state.nativeSortPending,"pre-sort opt-out was ignored");
    QueueDirectoryRefresh(state,true); settle();
    Check(!state.displayedSort.explicitOrder,"opt-out enumeration was sorted");
    // Submit real work, then enable pre-sorting before receiving its result.
    // The shared settings path must cancel that old generation even when
    // the folder-placement preference did not change.
    QueueDirectoryRefresh(state,true);
    const auto oldToken=state.directoryToken;
    g_preSort=true;
    const bool replace=ReconcileSortSettings(state);
    QueueDirectoryRefresh(state,replace,true);
    Check(replace && state.directoryToken!=oldToken,"pre-sort settings kept an in-flight unsorted generation");
    settle();
    Check(ReadShellSort(state,updated) && SameSort(state.displayedSort,updated),
        "pre-sort settings published the old generation under a sorted header");
    }
    Check(std::filesystem::weakly_canonical(root).parent_path()==std::filesystem::weakly_canonical(fixture),
        "initial sort cleanup escaped its private fixture");
    Check(Eventually([&] {
        std::error_code error; std::filesystem::remove_all(root,error); return !error;
    }),"initial sort fixture cleanup did not settle");
    std::cout<<"PASS: first-entry/native saved sorting, cached returns, both view-switch directions, header writes and opt-out\n";
}
