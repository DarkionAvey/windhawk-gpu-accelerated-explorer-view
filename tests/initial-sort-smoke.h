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
    const bool interactions=g_animateItemInteractions.exchange(false);
    const auto placement=g_folderPlacement.exchange(FolderPlacement::ExplorerDefault);
    struct Restore { bool interactions; FolderPlacement placement;
        ~Restore() {g_animateItemInteractions=interactions; g_folderPlacement=placement;}
    } restore{interactions,placement};
    {
    ProviderSelectionFixture::Browser host;
    auto& state=host.state;
    FocusFixture::Session focus(host.frame,state.hwnd);
    host.Navigate(root.wstring(),7);
    Check(state.sortInitialized && state.trackNativeSort &&
        state.sort.explicitOrder && SameSort(state.sort,state.displayedSort),"first folder ignored native sorting");
    const auto settle=[&](bool finishMotion=true) {
        Check(Eventually([&] {host.Pump(); ReceiveDirectoryResults(state);
            return state.loaded && !state.directoryPending;},30000),"initial sort publication");
        if (finishMotion) StopRowTransition(state);
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
    // DefView applies SetSortColumns asynchronously; under load its rows can
    // still be in the previous order after one pump. The GPU order is final,
    // so wait (bounded) for the native oracle to converge to it.
    const auto awaitNativeOrder=[&](const char* message,const PROPERTYKEY* key=nullptr,int direction=0) {
        std::vector<std::wstring> paths;
        const bool converged=Eventually([&] {
            host.Pump(); paths=nativeOrder();
            if(int(paths.size())!=state.total) return false;
            for(int i=0;i<state.total;++i) if(_wcsicmp(paths[i].c_str(),state.entries[i].path.c_str())) return false;
            return true;
        },30000);
        if(!converged) {
            SortSpec native;
            const bool readable=ReadShellSort(state,native);
            std::cout<<"NATIVE SORT readable="<<readable<<" direction="<<int(native.direction)
                <<" GPU direction="<<int(state.sort.direction)<<std::endl;
        }
        if(!converged) for(int i=0;i<std::min<int>(state.total,int(paths.size()));++i)
            if(_wcsicmp(paths[i].c_str(),state.entries[i].path.c_str()))
                std::wcout<<L"SORT MISMATCH key="<<(key ? key->pid : 0)<<L" direction="<<direction
                    <<L" row="<<i<<L" native="<<paths[i]<<L" GPU="<<state.entries[i].path<<std::endl;
        Check(converged,message);
    };
    // A cached return retains the folder's saved native sort.
    auto folder=state.folder;
    ResetForNewDirectory(state,root.wstring(),folder,false,state.activeTabHost); settle();
    Check(state.sort.explicitOrder && SameSort(state.sort,state.displayedSort),"cached return lost the native sort");
    for (const auto& key : {PKEY_ItemNameDisplay,PKEY_Size,PKEY_DateModified})
        for (const auto direction : {SORT_ASCENDING,SORT_DESCENDING}) {
            SORTCOLUMN column{key,direction};
            Check(SUCCEEDED(state.folderView->SetSortColumns(&column,1)),"native saved sort choice");
            host.Pump();
            // Re-enter through the real reset/cache path, without a GPU click.
            auto folder=state.folder;
            ResetForNewDirectory(state,root.wstring(),folder,false,state.activeTabHost);
            settle();
            awaitNativeOrder("saved sort differs from native view order",&key,int(direction));
        }
    // Native changes made while the overlay is off must replace its cache.
    SetActive(state.hwnd,false);
    SORTCOLUMN native{PKEY_Size,SORT_ASCENDING};
    Check(SUCCEEDED(state.folderView->SetSortColumns(&native,1)),"native-only sort choice"); host.Pump();
    SetActive(state.hwnd,true); settle();
    Check(SameKey(state.sort.key,PKEY_Size) && state.sort.direction==SORT_ASCENDING &&
        SameSort(state.sort,state.displayedSort),"native-to-overlay switch retained the old sort");
    // Isolated component input replay: no cursor/focus changes on the input
    // desktop. Exercise the complete press/capture/release path, not SortByHeader.
    state.deferColumnProviders=false; state.featureInitializationStep=std::max(3u,state.featureInitializationStep);
    FinishColumnTransition(state); RefreshExplorerColumns(state,true);
    Check(!state.columns.empty(),"native header columns");
    const auto bounds=ColumnBounds(state,0,true);
    const int headerX=LogicalX(state,static_cast<int>(ListStartX(state)+(bounds.first+bounds.second)*.5f));
    const int headerY=ColumnHeaderHeight(state)/2;
    PointerFixture::Session pointer;
    ModifierFixture::Session modifiers;
    const auto headerInput=[&](UINT message,int x,int y) {
        // Hardware input follows posted work in the normal Windows queue.
        // Dispatch that work between component input events, without waiting
        // for the GPU enumeration or inventing a double-click timing delay.
        host.Pump();
        PointerFixture::At(state.hwnd,POINT{x,y});
        PointerFixture::held=message!=WM_LBUTTONUP;
        return OverlayProc(state.hwnd,message,PointerFixture::held ? MK_LBUTTON : 0,MAKELPARAM(x,y));
    };
    const SortSpec beforeClick=state.sort;
    headerInput(WM_LBUTTONDOWN,headerX,headerY);
    Check(state.columnDragArmed && GetCapture()==state.hwnd && SameSort(beforeClick,state.sort),
        "header press did not capture or sorted before release");
    headerInput(WM_LBUTTONUP,headerX,headerY);
    const SortSpec firstClickSort=state.sort;
    Check(SameKey(firstClickSort.key,state.columns.front().key) && !SameSort(firstClickSort,beforeClick),
        "header release did not apply its first sort");
    headerInput(WM_LBUTTONDBLCLK,headerX,headerY);
    Check(state.columnDragArmed && GetCapture()==state.hwnd && SameSort(firstClickSort,state.sort),
        "double-click did not replay the header press/capture path");
    headerInput(WM_LBUTTONUP,headerX,headerY);
    Check(SameKey(state.sort.key,firstClickSort.key) &&
        state.sort.direction==(firstClickSort.direction==SORT_ASCENDING ? SORT_DESCENDING : SORT_ASCENDING),
        "rapid second header click did not reverse the first sort");
    settle(false);
    Check(!state.rowTransitionActive && !state.instantInteractionSort && !state.columnDragArmed &&
        GetCapture()!=state.hwnd,"completed header double-click retained animation, intent or capture");
    SortSpec updated;
    Check(ReadShellSort(state,updated) && SameSort(updated,state.sort) &&
        SameKey(updated.key,state.columns.front().key),"overlay header did not update native sorting");
    awaitNativeOrder("rapid double header sort differs from native order");
    // A second press is also allowed to become a drag. Vertical movement
    // crosses the system threshold without changing the insertion slot.
    headerInput(WM_LBUTTONDOWN,headerX,headerY);
    headerInput(WM_LBUTTONUP,headerX,headerY);
    const SortSpec beforeDrag=state.sort;
    headerInput(WM_LBUTTONDBLCLK,headerX,headerY);
    const int dragY=headerY+GetSystemMetrics(SM_CYDRAG)+1;
    headerInput(WM_MOUSEMOVE,headerX,dragY);
    Check(state.columnDragActive && GetCapture()==state.hwnd,"double-click second press could not become a drag");
    headerInput(WM_LBUTTONUP,headerX,dragY);
    Check(SameSort(beforeDrag,state.sort) && !state.columnDragArmed && GetCapture()!=state.hwnd,
        "header drag sorted on release or retained capture");
    settle(false);
    // A reversal really moves the private rows. Replace its pending worker
    // generation, as a concurrent refresh would, without losing input policy.
    SortByHeader(state,0);
    QueueDirectoryRefresh(state,true,true);
    settle(false);
    Check(!state.rowTransitionActive && state.rowMotion.empty() && !state.instantInteractionSort,
        "replacement refresh animated a deliberate header sort");
    awaitNativeOrder("instant header order differs from native");
    // Keep the internal switch functional before it becomes a user option.
    g_animateItemInteractions=true;
    SortByHeader(state,0); settle(false);
    Check(state.rowTransitionActive,"enabled item-interaction flag did not animate header sorting");
    StopRowTransition(state); g_animateItemInteractions=false;
    // An explicit native/menu command supersedes a still-pending header intent,
    // even when it selects that same order. Use the real Shell setter.
    SortByHeader(state,0);
    SORTCOLUMN menu{state.sort.key,state.sort.direction};
    Check(SUCCEEDED(state.folderView->SetSortColumns(&menu,1)),"native sort after pending header");
    RefreshSortFromExplorer(state,true,true,true); settle(false);
    Check(state.rowTransitionActive && !state.instantInteractionSort,"native menu inherited an instant header policy");
    StopRowTransition(state);
    Check(SUCCEEDED(state.folderView->SetGroupBy(PKEY_ItemTypeText,TRUE)),"private grouped header sort");
    RefreshSortFromExplorer(state,true,true); settle();
    SortByHeader(state,0); settle(false);
    Check(!state.rowTransitionActive && state.groupHeaderMotion.empty() && !state.instantInteractionSort,
        "grouped header sort animated items or group headings");
    Check(SUCCEEDED(state.folderView->SetGroupBy(PKEY_Null,TRUE)),"private header group reset");
    RefreshSortFromExplorer(state,true,true); settle();
    const SORTCOLUMN multiple[]={{PKEY_ItemTypeText,SORT_ASCENDING},{PKEY_DateModified,SORT_DESCENDING}};
    Check(SUCCEEDED(state.folderView->SetSortColumns(multiple,2)),"native secondary sort choice"); host.Pump();
    RefreshSortFromExplorer(state,true,true); settle();
    Check(state.sort.secondary.size()==1 && SameSort(state.sort,state.displayedSort),"secondary native key was dropped");
    awaitNativeOrder("secondary sort differs from native order");
    state.folderCache.folders.clear();
    InitializeFolderSort(state,root.wstring());
    Check(state.sort.explicitOrder && !state.nativeSortPending,"initialization did not retain the native sort");
    QueueDirectoryRefresh(state,true); settle();
    Check(ReadShellSort(state,updated) && SameSort(state.displayedSort,updated),"directory refresh did not use the native sort");
    }
    Check(std::filesystem::weakly_canonical(root).parent_path()==std::filesystem::weakly_canonical(fixture),
        "initial sort cleanup escaped its private fixture");
    Check(ReleasePrivateFixture(root),"initial sort fixture cleanup did not settle");
    std::cout<<"PASS: saved/native sorting, cached returns, view switches, instant/grouped header writes, pending replacements, internal animation switch and animated native commands\n";
}
