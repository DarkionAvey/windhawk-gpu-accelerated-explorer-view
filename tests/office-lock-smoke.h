// Real asynchronous names/layout publication and selection indices from a
// private directory. Never open Office, touch user files or change Shell options.
template<class Predicate>
static void ProfileOfficeNameFilter(Predicate predicate) {
    // Isolate the added predicate from filesystem/Shell costs using the exact
    // enumeration records. This is CPU-only, not an Explorer latency/FPS test.
    constexpr size_t count=100000;
    std::vector<WIN32_FIND_DATAW> records(count);
    size_t expectedBase=0,expectedFiltered=0;
    volatile bool showHidden=true,showProtected=false;
    const ShellVisibilityPolicy policy{showHidden,showProtected};
    for(size_t i=0;i<count;++i) {
        auto& record=records[i];
        const bool owner=i%997==0,folder=i%13==0;
        const bool system=i%31==0;
        record.dwFileAttributes=(folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE) |
            (i%17==0 ? FILE_ATTRIBUTE_HIDDEN : 0) | (system ? FILE_ATTRIBUTE_SYSTEM : 0);
        const auto name=std::wstring(owner ? L"~$document-" : L"document-")+std::to_wstring(i)+L".docx";
        wcscpy_s(record.cFileName,name.c_str());
        expectedBase+=!system; expectedFiltered+=!system && !(owner && !folder);
    }
    std::vector<double> baseline,filtered;
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    std::atomic<size_t> checksum{0};
    const auto measure=[&]<bool names>() {
        std::atomic_signal_fence(std::memory_order_seq_cst);
        LARGE_INTEGER begin{},end{}; QueryPerformanceCounter(&begin);
        size_t visible=0;
        for(const auto& record:records) {
            if constexpr(requires {predicate(record.dwFileAttributes,policy,record.cFileName);})
                visible+=predicate(record.dwFileAttributes,policy,names ? record.cFileName : nullptr);
            else visible+=predicate(record.dwFileAttributes,policy); // Historical negative control.
        }
        QueryPerformanceCounter(&end);
        checksum.fetch_add(visible,std::memory_order_relaxed);
        Check(visible==(names ? expectedFiltered : expectedBase),"Office predicate benchmark result mismatch");
        return 1000.*(end.QuadPart-begin.QuadPart)/frequency.QuadPart;
    };
    for(int pass=0;pass<42;++pass) {
        double a=0,b=0;
        if(pass%2) {b=measure.template operator()<true>(); a=measure.template operator()<false>();}
        else {a=measure.template operator()<false>(); b=measure.template operator()<true>();}
        if(pass>=2) {baseline.push_back(a); filtered.push_back(b);}
    }
    std::sort(baseline.begin(),baseline.end()); std::sort(filtered.begin(),filtered.end());
    const double base=baseline[baseline.size()/2],withNames=filtered[filtered.size()/2];
    Check(checksum.load()==42*(expectedBase+expectedFiltered),"Office benchmark was not consumed");
    std::cout<<"PROFILE: 100000 cached enumeration records attribute-only median="<<base
        <<" ms; with Office prefix median="<<withNames<<" ms; added="<<withNames-base
        <<" ms ("<<(withNames-base)*1000000./count<<" ns/entry); predicate only, no filesystem/Shell calls\n";
}

static void TestOfficeLockFiltering(const std::wstring& fixture) {
    const auto directory=JoinPath(fixture,L"office-owner-files");
    Check(CreateDirectoryW(directory.c_str(),nullptr)!=FALSE,"Office lock private directory");
    std::vector<std::wstring> files,locks;
    const auto create=[&](const std::wstring& name,DWORD attributes) {
        const auto path=JoinPath(directory,name);
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"Office lock private file"); CloseHandle(file);
        Check(SetFileAttributesW(path.c_str(),attributes)!=FALSE,"Office lock fixture attributes");
        files.push_back(path); return path;
    };
    for(const auto* name:{L"~$CS protocol - Adapted - PTO(1).docx",L"~$budget.xlsx",L"~$slides.pptx",
        L"~$legacy.doc",L"~$macros.xlsm",L"~$UPPER.DOCX",L"~$pending.tmp",L"~$"})
        locks.push_back(create(name,locks.size()%2 ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_ARCHIVE));
    for(const auto* name:{L"document.docx",L"budget.xlsx",L"slides.pptx",L"~report.docx",L"$report.docx",
        L"embedded~$owner.docx",L"~",L"$"}) create(name,FILE_ATTRIBUTE_NORMAL);
    create(L"working.tmp",FILE_ATTRIBUTE_TEMPORARY);
    create(L"ordinary-hidden.docx",FILE_ATTRIBUTE_HIDDEN);
    create(L"protected.docx",FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM);
    const auto folder=JoinPath(directory,L"~$folder.docx");
    Check(CreateDirectoryW(folder.c_str(),nullptr)!=FALSE,"owner-like folder fixture");
    ViewState state;
    state.hwnd=MessageWindow(); state.nativeHwnd=state.hwnd; state.activeTabHost=state.frameHwnd=state.hwnd;
    state.background->hwnd=state.hwnd; state.folderPath=directory;
    state.inScope=state.detailsViewActive=true; state.columnsView=false;
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    struct Cleanup {
        ViewState& state; std::vector<std::wstring>& files; const std::wstring& folder; const std::wstring& directory;
        ~Cleanup() {
            Stop(state.background); SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0); DestroyWindow(state.hwnd);
            for(const auto& path:files) {SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL); DeleteFileW(path.c_str());}
            RemoveDirectoryW(folder.c_str()); RemoveDirectoryW(directory.c_str());
        }
    } cleanup{state,files,folder,directory};
    ComPtr<IShellItem> item;
    Check(SUCCEEDED(SHCreateItemFromParsingName(directory.c_str(),nullptr,IID_PPV_ARGS(&item))) &&
        SUCCEEDED(item->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&state.folder))),"Office selection private folder");
    auto* native=new SelectionFolderView; native->state=&state; native->folder=state.folder;
    native->reenterCapture=false; state.folderView.Attach(native);
    const auto noOwners=[&](const std::vector<Entry>& entries) {
        for(const auto& entry:entries) Check(entry.folder || entry.name.rfind(L"~$",0)!=0,
            "Office lock file entered a preview, completed model or cache");
    };
    const auto refresh=[&] {
        QueueDirectoryRefresh(state,true);
        DirectoryResult complete;
        Check(Eventually([&] {
            ExclusiveSrwLock lock(state.background->directoryLock);
            for(auto it=state.background->directoryResults.begin();it!=state.background->directoryResults.end();) {
                if(it->token!=state.directoryToken ||
                    (it->kind!=DirectoryResult::Preview && it->kind!=DirectoryResult::Complete)) {++it; continue;}
                noOwners(it->entries);
                if(it->kind==DirectoryResult::Complete) {complete=std::move(*it); state.background->directoryResults.erase(it); return true;}
                it=state.background->directoryResults.erase(it);
            }
            return false;
        }),"Office-filtered directory completion");
        Check(complete.error==ERROR_SUCCESS,"Office-filtered enumeration error");
        PublishDirectoryResult(*state.background,std::move(complete)); ReceiveDirectoryResults(state);
        noOwners(state.entries);
    };
    for(bool hidden:{false,true}) for(bool protectedFiles:{false,true}) for(bool sorted:{false,true}) {
        state.visibility={hidden,protectedFiles};
        state.sort=DefaultSort(); state.sort.explicitOrder=sorted;
        state.sort.kind=SortKind::Name; state.sort.key=PKEY_FileName; state.sort.direction=SORT_ASCENDING;
        refresh();
        const int expected=10+int(hidden)+int(hidden && protectedFiles);
        Check(state.loaded && state.total==expected && state.entryPathIndex.size()==size_t(expected),
            "Office filtering changed unrelated files, folders or hidden/protected policy");
        for(const auto& path:locks) Check(GpuRowForNativePath(state,path)==-1,"Office lock retained a selectable row identity");
        Check(GpuRowForNativePath(state,folder)>=0,"Office name filter hid a legitimate directory");
        for(int row=0;row<state.total;++row) Check(GpuRowForNativePath(state,state.entries[row].path)==row,
            "Office filtering left gaps or stale row indices");
        Check(KeyboardNavigationTarget(state,VK_END)==state.total-1,"End navigated outside the filtered model");
    }
    state.selected=GpuRowForNativePath(state,JoinPath(directory,L"document.docx"));
    state.selectedFlags.assign(state.total,1); native->allCountOverride=state.total;
    // Exercise the real GPU -> Shell PIDL selection transaction on the filtered
    // model. The substituted native view only stores private fixture identities.
    SyncNativeSelection(state);
    Check(!state.nativeSelectionUnsafe && native->selected.size()==size_t(state.total),
        "filtered all-items selection failed native reconciliation");
    const auto selectedPaths=[&] {
        std::vector<std::wstring> paths;
        for(int row=0;row<state.total;++row) if(state.selectedFlags[row]) paths.push_back(state.entries[row].path);
        return paths;
    };
    const auto selected=selectedPaths();
    const auto order=state.displayedOrderHash,metadata=state.displayedMetadataHash;
    const auto live=create(L"~$live.docx",FILE_ATTRIBUTE_NORMAL);
    refresh();
    Check(state.displayedOrderHash==order && state.displayedMetadataHash==metadata && selectedPaths()==selected,
        "creating an Office owner file changed layout/selection");
    Check(DeleteFileW(live.c_str())!=FALSE,"private Office owner removal"); refresh();
    Check(state.displayedOrderHash==order && state.displayedMetadataHash==metadata && selectedPaths()==selected,
        "removing an Office owner file changed layout/selection");
    PrimeSnapshotWatch(state,directory); // Shared private cache fixture; no live watch pump in this test.
    CacheDepartingFolder(state);
    Check(RestoreCachedFolder(state),"filtered directory cache return"); noOwners(state.entries);
    Check(state.total==12 && state.entryPathIndex.size()==12 && GpuRowForNativePath(state,locks.front())==-1,
        "warm cached folder reintroduced an Office lock identity");
    std::cout<<"PASS: Office ~$ files excluded before asynchronous preview/layout/index/cache publication under all hidden/protected policies; normal/temp files and owner-like folders remain, End/native bulk selection and owner creation/removal preserve the filtered model\n";
    ProfileOfficeNameFilter(ShellItemVisible);
}
