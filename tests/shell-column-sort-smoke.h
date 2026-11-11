// Real private providers; native DefView indices are the independent oracle.
namespace ShellColumnSortFixture {
// Malformed image contents intentionally exercise property-store failure;
// native folder columns still have their filesystem dates, sizes and types.
inline const std::array<std::wstring,6> names{L"z.txt",L"a.png",L"m.bin",L"item10.txt",L"item2.txt",L"b.png"};
void WriteFiles(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    for (size_t i=0;i<names.size();++i) {
        HANDLE file=CreateFileW((root/names[i]).c_str(),GENERIC_WRITE|FILE_WRITE_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"column sort private file");
        const char bytes[16]{}; DWORD written=0;
        SYSTEMTIME date{};date.wYear=WORD(i<2 ? 1850 : 2020+i/2);date.wMonth=1;date.wDay=2;
        date.wHour=12;FILETIME stamp{}; // Includes dates the Shell property routes can omit.
        const bool ok=WriteFile(file,bytes,DWORD(i+1),&written,nullptr) &&
            SystemTimeToFileTime(&date,&stamp) && SetFileTime(file,&stamp,&stamp,&stamp);
        CloseHandle(file); Check(ok,"column sort private metadata");
    }
}
std::vector<Entry> Rows(ProviderSelectionFixture::Browser& host) {
    auto& state=host.state;
    std::vector<Entry> entries;
    PIDLIST_ABSOLUTE raw=nullptr;
    Check(SUCCEEDED(SHGetIDListFromObject(state.folder.Get(),&raw)) && raw,"column sort parent identity");
    OwnedPidl parent(raw);
    for (int i=0;i<state.total;++i) {
        PITEMID_CHILD child=nullptr;
        Check(SUCCEEDED(state.folderView->Item(i,&child)) && child,"column sort native child");
        NativeRowIdentity identity;
        identity.child.reset(child);identity.absolute.reset(ILCombine(parent.get(),child));identity.namespaceItem=true;
        Entry entry;
        Check(identity.absolute && SUCCEEDED(ResolveNativeRowIdentity(identity,entry,state.folder.Get())),
            "column sort namespace identity");
        entries.push_back(std::move(entry));
    }
    return entries;
}
std::filesystem::path FilePath(const Entry& entry) {
    PWSTR raw=nullptr;
    const HRESULT hr=SHGetNameFromIDList(entry.shellIdentity.get(),SIGDN_FILESYSPATH,&raw);
    std::unique_ptr<wchar_t,TaskMemDeleter> path(raw);
    Check(SUCCEEDED(hr) && path,"private filesystem identity path");
    return path.get();
}
void NativeOrder(ProviderSelectionFixture::Browser& host,const std::vector<Entry>& sorted) {
    const bool matched=Eventually([&] {
        host.Pump();int count=-1;
        if (FAILED(host.state.folderView->ItemCount(SVGIO_ALLVIEW,&count)) || count!=int(sorted.size())) return false;
        for (int i=0;i<count;++i) {
            PITEMID_CHILD raw=nullptr;
            if (FAILED(host.state.folderView->Item(i,&raw)) || !raw) return false;
            OwnedPidl child(raw);
            if (!sorted[size_t(i)].shellChild) return false;
            const HRESULT equal=host.state.folder->CompareIDs(SHCIDS_CANONICALONLY,child.get(),sorted[size_t(i)].shellChild.get());
            if (FAILED(equal) || HRESULT_CODE(equal)!=0) return false;
        }
        return true;
    },30000);
    if (!matched) for (size_t i=0;i<sorted.size();++i) {
        PITEMID_CHILD raw=nullptr; STRRET text{}; PWSTR name=nullptr;
        if (SUCCEEDED(host.state.folderView->Item(int(i),&raw)) && raw) {
            OwnedPidl child(raw);
            if (SUCCEEDED(host.state.folder->GetDisplayNameOf(child.get(),SHGDN_INFOLDER,&text)))
                StrRetToStrW(&text,child.get(),&name);
        }
        std::wcout<<L"COLUMN ORDER row="<<i<<L" native="<<(name ? name : L"<missing>")
            <<L" GPU="<<sorted[i].name<<L" kind="<<unsigned(sorted[i].sortValueKind)<<L" key="<<sorted[i].sortNumber
            <<L" namespace="<<sorted[i].path.starts_with(L"windhawk:pidl:")<<std::endl;
        CoTaskMemFree(name);
    }
    Check(matched,"column sort differs from native DefView order");
}
}
static void TestShellColumnSort(const std::wstring& fixture,bool archive=false) {
    using namespace ShellColumnSortFixture;
    // Pure conversion check, separate from real provider/oracle coverage.
    SYSTEMTIME oldDate{};oldDate.wYear=1850;oldDate.wMonth=1;oldDate.wDay=2;oldDate.wHour=12;
    PROPVARIANT automation{};automation.vt=VT_DATE;FILETIME expected{};Entry converted;
    Check(SystemTimeToVariantTime(&oldDate,&automation.date) && SystemTimeToFileTime(&oldDate,&expected) &&
        AssignExtraShellSortValue(converted,PKEY_DateModified,automation) &&
        converted.sortValueKind==2 && converted.sortNumber==static_cast<double>(FileTimeNumber(expected)),
        "negative fractional Automation date did not use FILETIME units");
    const auto root=std::filesystem::path(fixture)/L"shell-column-sort";
    WriteFiles(root);
    std::filesystem::path destination=root;
    if (archive) {
        std::vector<ProviderSelectionFixture::Member> members;
        for (size_t i=0;i<names.size();++i)
            members.push_back({std::filesystem::path(names[i]).string(),std::string(i+1,'x')});
        destination=root/L"private.zip";
        ProviderSelectionFixture::WriteZip(destination,ProviderSelectionFixture::ZipBytes(std::move(members)));
    }
    {
    ProviderSelectionFixture::Browser host;
    FocusFixture::Session focus(host.frame,host.state.hwnd);
    host.Navigate(destination.wstring(),int(names.size()));
    auto entries=Rows(host);
    ViewState worker;constexpr unsigned long long token=41;
    worker.background->directoryGeneration=token;
    for (const PROPERTYKEY key:{PKEY_DateModified,PKEY_ItemTypeText,PKEY_Size})
        for (const SORTDIRECTION direction:{SORT_ASCENDING,SORT_DESCENDING}) {
            Regression::Scope scope("column="+std::to_string(key.pid)+" direction="+std::to_string(direction));
            SortSpec spec=DefaultSort();spec.key=key;spec.kind=ClassifySort(key);
            spec.direction=direction;spec.explicitOrder=true;spec.placement=FolderPlacement::Mixed;
            spec.secondary={{PKEY_ItemNameDisplay,direction}};
            const SORTCOLUMN columns[]={{key,direction},{PKEY_ItemNameDisplay,direction}};
            Check(SUCCEEDED(host.state.folderView->SetSortColumns(columns,2)),"column sort native choice");
            std::map<std::wstring,std::wstring> types;
            auto sorted=entries;
            SortDirectoryEntries(sorted,spec,types,*worker.background,token);
            NativeOrder(host,sorted);
            // Mix the real column and real property-store routes in one sort.
            // Neither provider responses nor expected comparisons are fabricated.
            auto mixed=entries;ShellSizeBinding sizes;ShellColumnBinding binding;
            for (size_t i=0;i<mixed.size();++i) {
                ReadExtraShellSortValue(mixed[i],key,
                    i%2==0 && SameKey(key,PKEY_Size) ? &sizes : nullptr,
                    i%2==0 && !SameKey(key,PKEY_Size) ? &binding : nullptr,token);
                Check(mixed[i].sortValueKind!=0,"column/store route did not resolve its value");
                if (SameKey(key,PKEY_DateModified) && !archive) {
                    WIN32_FILE_ATTRIBUTE_DATA data{};
                    Check(GetFileAttributesExW(FilePath(mixed[i]).c_str(),GetFileExInfoStandard,&data),"independent sort timestamp");
                    const ULONGLONG expected=(ULONGLONG(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime;
                    Check(mixed[i].sortValueKind==2 && mixed[i].sortNumber==static_cast<double>(expected),
                        "column/store timestamp did not use canonical FILETIME units");
                }
            }
            spec.kind=SortKind::ShellProperty;
            SortEntriesByIndex(mixed,[&](const Entry& a,const Entry& b) {return EntryPrecedes(a,b,spec);});
            NativeOrder(host,mixed);
        }
    const SORTCOLUMN columns[]={{PKEY_ItemTypeText,SORT_ASCENDING},{PKEY_DateModified,SORT_DESCENDING},
        {PKEY_ItemNameDisplay,SORT_ASCENDING}};
    Check(SUCCEEDED(host.state.folderView->SetSortColumns(columns,3)),"native secondary property choice");
    SortSpec spec=DefaultSort();spec.key=PKEY_ItemTypeText;spec.kind=SortKind::Type;
    spec.explicitOrder=true;spec.placement=FolderPlacement::Mixed;spec.secondary={columns[1],columns[2]};
    std::map<std::wstring,std::wstring> types;
    SortDirectoryEntries(entries,spec,types,*worker.background,token);
    NativeOrder(host,entries);Stop(worker.background);
    }
    Check(ReleasePrivateFixture(root),"column sort private fixture cleanup");
    std::cout<<"PASS: native DefView ordering, column/store dates, ties and secondary properties\n";
}
static void TestShellSortCheck(const std::wstring& fixture) {
    using namespace ShellColumnSortFixture;
    const auto root=std::filesystem::path(fixture)/L"shell-sort-check";
    WriteFiles(root);
    {
    ProviderSelectionFixture::Browser host;
    FocusFixture::Session focus(host.frame,host.state.hwnd);
    host.Navigate(root.wstring(),int(names.size()));
    auto rows=Rows(host);
    ViewState worker;worker.hwnd=MessageWindow();worker.background->hwnd=worker.hwnd;
    constexpr unsigned long long token=51;worker.background->directoryGeneration=token;
    Check(StartTabWorker(worker.background,worker.background->layout,DirectoryWorkerProc,THREAD_PRIORITY_NORMAL),
        "targeted sort check worker");
    const auto check=[&](const Entry& before,const SortSpec& sort,bool changed) {
        DirectoryJob job;job.hwnd=worker.hwnd;job.token=token;job.path=root.wstring();
        job.sort=sort;job.checkSort=true;job.sortChecks.push_back(SortEntrySnapshot(before,true));
        {
            ExclusiveSrwLock lock(worker.background->directoryLock);
            worker.background->layoutJobs.push_back(std::move(job));SetEvent(worker.background->layout.wake);
        }
        std::optional<DirectoryResult> result;
        Check(Eventually([&] {
            ExclusiveSrwLock lock(worker.background->directoryLock);
            for (auto it=worker.background->directoryResults.begin();it!=worker.background->directoryResults.end();++it)
                if (it->kind==DirectoryResult::SortCheck) {
                    result=std::move(*it);worker.background->directoryResults.erase(it);return true;
                }
            return false;
        },30000),"targeted sort check completion");
        if (result->sortChanged!=changed) std::cout<<"SORT CHECK expected="<<changed<<" actual="<<result->sortChanged
            <<" previousKind="<<unsigned(before.sortValueKind)<<" previous="<<before.sortNumber<<std::endl;
        Check(result->sortChanged==changed && (!changed || result->sortChangedPath==before.path),
            "targeted check missed real changed data or refreshed unchanged data");
    };
    unsigned revision=0;
    for (const PROPERTYKEY key:{PKEY_DateModified,PKEY_DateCreated,PKEY_DateAccessed,PKEY_Size})
        for (bool secondary:{false,true}) {
            Regression::Scope scope("key="+std::to_string(key.pid)+" secondary="+std::to_string(secondary));
            Entry before=rows.front();Check(RefreshSortCheckIdentity(before),"targeted baseline identity refresh");
            SortSpec spec=DefaultSort();spec.explicitOrder=true;
            spec.key=secondary ? PKEY_ItemNameDisplay : key;spec.kind=ClassifySort(spec.key);
            if (secondary) spec.secondary={{key,SORT_ASCENDING}};
            std::vector<Entry> baseline{before};std::map<std::wstring,std::wstring> types;
            SortDirectoryEntries(baseline,spec,types,*worker.background,token);before=std::move(baseline.front());
            check(before,spec,false);
            HANDLE file=CreateFileW(FilePath(before).c_str(),GENERIC_WRITE|FILE_WRITE_ATTRIBUTES,
                FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            Check(file!=INVALID_HANDLE_VALUE,"targeted change private file");
            SYSTEMTIME date{};date.wYear=WORD(2030+revision++);date.wMonth=2;date.wDay=3;FILETIME stamp{};
            bool changed=SystemTimeToFileTime(&date,&stamp)!=FALSE;
            if (SameKey(key,PKEY_Size)) changed=changed && SetFilePointer(file,100+revision,nullptr,FILE_BEGIN)!=INVALID_SET_FILE_POINTER && SetEndOfFile(file);
            else changed=changed && SetFileTime(file,SameKey(key,PKEY_DateCreated) ? &stamp : nullptr,
                SameKey(key,PKEY_DateAccessed) ? &stamp : nullptr,SameKey(key,PKEY_DateModified) ? &stamp : nullptr);
            CloseHandle(file);Check(changed,"targeted real metadata mutation");check(before,spec,true);
        }
    Entry missing=rows.front();Check(DeleteFileW(FilePath(missing).c_str()),"targeted deleted item");
    SortSpec name=DefaultSort();name.explicitOrder=true;check(missing,name,true);
    Stop(worker.background);DestroyWindow(worker.hwnd);
    }
    Check(ReleasePrivateFixture(root),"targeted sort fixture cleanup");
    std::cout<<"PASS: targeted namespace primary/secondary checks refresh cached dates/sizes and detect deletion\n";
}
