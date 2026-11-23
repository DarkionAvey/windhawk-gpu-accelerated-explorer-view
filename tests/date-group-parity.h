static void TestDateGroupParity(const std::wstring& fixture) {
    wchar_t desktop[256]{};
    DWORD bytes{};
    Check(GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME,
        desktop, sizeof(desktop), &bytes), "date group desktop");
    if (!std::wstring_view(desktop).starts_with(L"gpu-overlay-date-groups-")) {
        PrivateDesktopFixture::Run(L"date-groups", L"--test native-date-group-parity", 90000);
        return;
    }
    const auto root = std::filesystem::path(fixture) / L"date-groups";
    std::filesystem::create_directory(root);
    constexpr int offsets[] = {-410,-75,-22,-9,-5,-2,-1,0,0,1,4,12,45,410};
    constexpr int count = ARRAYSIZE(offsets);
    SYSTEMTIME noon{};
    GetLocalTime(&noon);
    noon.wHour=12; noon.wMinute=noon.wSecond=noon.wMilliseconds=0;
    FILETIME base{};
    Check(SystemTimeToFileTime(&noon,&base), "fixture calendar date");
    auto stamp = [&](int index) {
        ULARGE_INTEGER value{};
        value.LowPart=base.dwLowDateTime; value.HighPart=base.dwHighDateTime;
        value.QuadPart += offsets[index % count] * 864000000000LL;
        FILETIME civil{value.LowPart,value.HighPart}, utc{};
        SYSTEMTIME local{}, system{};
        Check(FileTimeToSystemTime(&civil,&local) && TzSpecificLocalTimeToSystemTimeEx(nullptr,&local,&system)
            && SystemTimeToFileTime(&system,&utc), "fixture local noon");
        return utc;
    };
    for (int i=0;i<count;++i) {
        auto path=root/(L"sample"+std::to_wstring(i));
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"create dated fixture");
        auto created=stamp(i+3), accessed=stamp(i+8), modified=stamp(i);
        bool set=SetFileTime(file,&created,&accessed,&modified);
        CloseHandle(file); Check(set,"set fixture dates");
    }
    ProviderSelectionFixture::Browser host(false,true);
    host.Navigate(root.wstring(),count);
    auto& state=host.state;
    HWND list=nullptr;
    EnumChildWindows(host.frame,[](HWND hwnd,LPARAM context)->BOOL {
        wchar_t cls[64]{}; GetClassNameW(hwnd,cls,ARRAYSIZE(cls));
        if (wcscmp(cls,WC_LISTVIEWW)) return TRUE;
        *reinterpret_cast<HWND*>(context)=hwnd; return FALSE;
    },reinterpret_cast<LPARAM>(&list));
    Check(list!=nullptr,"native DefView list");
    using Groups=std::vector<std::pair<std::wstring,std::set<std::wstring>>>;
    for (auto key : {PKEY_DateModified,PKEY_DateCreated,PKEY_DateAccessed}) for (BOOL ascending : {TRUE,FALSE}) {
        Check(SUCCEEDED(state.folderView->SetGroupBy(key,ascending)),"native date group selection");
        ReconcileShellGrouping(state,true);
        Groups actual,expected;
        bool equal=Eventually([&] {
            host.Pump(); ReceiveDirectoryResults(state);
            if (state.directoryPending || !SameKey(state.displayedGrouping.key,key) ||
                state.displayedGrouping.ascending!=ascending) return false;
            expected.clear(); actual.clear();
            std::map<int,size_t> ids;
            int n=int(SendMessageW(list,LVM_GETGROUPCOUNT,0,0));
            for (int i=0;i<n;++i) {
                wchar_t title[256]{};
                LVGROUP g{sizeof(g)}; g.mask=LVGF_HEADER|LVGF_GROUPID;
                g.pszHeader=title; g.cchHeader=ARRAYSIZE(title);
                if (!SendMessageW(list,LVM_GETGROUPINFOBYINDEX,i,reinterpret_cast<LPARAM>(&g))) return false;
                ids[g.iGroupId]=expected.size(); expected.push_back({title,{}});
            }
            if (n<3 || ListView_GetItemCount(list)!=count) return false;
            for(int i=0;i<count;++i) {
                LVITEMW item{}; item.mask=LVIF_GROUPID; item.iItem=i;
                wchar_t name[128]{};
                if (!ListView_GetItem(list,&item) || !ids.count(item.iGroupId)) return false;
                ListView_GetItemText(list,i,0,name,ARRAYSIZE(name));
                expected[ids[item.iGroupId]].second.insert(name);
            }
            // Classic DefView appends the item count to the localized caption.
            // Strip only a suffix that agrees with the independently read membership.
            for (auto& [title,names] : expected) {
                auto suffix=L" ("+std::to_wstring(names.size())+L")";
                if (title.ends_with(suffix)) title.resize(title.size()-suffix.size());
            }
            for (const auto& group : state.sections) {
                actual.push_back({group.title,{}});
                for (int i=group.first;i<group.end;++i) actual.back().second.insert(state.entries.at(i).name);
            }
            return actual==expected;
        });
        if (!equal) {
            std::wcerr<<L"Date property "<<key.pid<<L" ascending "<<ascending<<L'\n';
            for(const auto* groups : {&expected,&actual}) {
                std::wcerr<<(groups==&expected ? L"Native" : L"Mod")<<L'\n';
                for(const auto& [title,names] : *groups) {
                    std::wcerr<<title; for(const auto& name:names) std::wcerr<<L" "<<name;
                    std::wcerr<<L'\n';
                }
            }
        }
        Check(equal,"native date sections and members differ");
    }
}
