// Real Shell details and the real asynchronous detail lane, using only a
// private fixture. No Explorer injection, replacement providers or messages.
static void TestShellSizeDetails(const std::wstring& directory) {
    const auto root = std::filesystem::path(directory) / L"shell-size-details";
    std::filesystem::create_directories(root / L"folder");
    for (const auto& name : {L"empty.bin", L"large.bin"}) {
        HANDLE file = CreateFileW((root / name).c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "size fixture file");
        LARGE_INTEGER length{};
        length.QuadPart = !wcscmp(name, L"large.bin") ? 5 * 1024 * 1024 + 17 : 0;
        const bool sized = SetFilePointerEx(file, length, nullptr, FILE_BEGIN) && SetEndOfFile(file);
        CloseHandle(file);
        Check(sized, "size fixture length");
    }
    ComPtr<IShellItem> item;
    ComPtr<IShellFolder2> folder;
    Check(SUCCEEDED(SHCreateItemFromParsingName(root.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
        SUCCEEDED(item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&folder))),
        "size fixture Shell folder");
    UINT sizeColumn = 0;
    for (;; ++sizeColumn) {
        PROPERTYKEY key{};
        Check(sizeColumn < kMaxShellColumns &&
            SUCCEEDED(folder->MapColumnToSCID(sizeColumn, &key)), "Shell Size column");
        if (SameKey(key, PKEY_Size)) break;
    }
    ViewState state;
    state.hwnd = MessageWindow();
    state.background->hwnd = state.hwnd;
    state.folderPath = root.wstring();
    state.rowPx = 34;
    QueueDirectoryRefresh(state);
    Check(Eventually([&] {
        ReceiveDirectoryResults(state);
        return state.loaded && !state.directoryPending;
    }), "size fixture names completion");
    Check(state.total == 3, "size fixture row count");
    for (bool identity : {false, true}) {
        DetailJob job;
        job.hwnd = state.hwnd;
        job.token = state.directoryToken;
        job.columnEpoch = state.columnEpoch;
        std::vector<std::wstring> expected;
        for (int row = 0; row < state.total; ++row) {
            const auto& entry = state.entries[size_t(row)];
            PIDLIST_RELATIVE child = nullptr;
            auto name = entry.name;
            Check(SUCCEEDED(folder->ParseDisplayName(nullptr, nullptr, name.data(),
                nullptr, &child, nullptr)), "size fixture child");
            OwnedPidl owned(child);
            SHELLDETAILS native{};
            PWSTR text = nullptr;
            Check(SUCCEEDED(folder->GetDetailsOf(child, sizeColumn, &native)) &&
                SUCCEEDED(StrRetToStrW(&native.str, child, &text)), "native Shell size text");
            expected.emplace_back(text ? text : L"");
            CoTaskMemFree(text);
            if(entry.folder ? !expected.back().empty() : expected.back().empty())
                std::wcout << L"SIZE native name=" << entry.name << L" folder=" << entry.folder
                    << L" bytes=" << entry.byteSize << L" attributes=" << GetFileAttributesW(entry.path.c_str())
                    << L" text=[" << expected.back() << L"] column=" << sizeColumn << std::endl;
            Check(entry.folder ? expected.back().empty() : !expected.back().empty(),
                "native size must distinguish folders from zero-byte files");
            DetailRequest request;
            request.index = row; request.path = entry.path;
            request.folder = entry.folder; request.size = entry.byteSize;
            request.missingKeys.push_back(PKEY_Size);
            if (identity) {
                PIDLIST_ABSOLUTE absolute = nullptr;
                Check(SUCCEEDED(SHParseDisplayName(entry.path.c_str(), nullptr, &absolute, 0, nullptr)),
                    "size fixture absolute identity");
                request.shellIdentity = SharedPidl(absolute, TaskMemDeleter{});
            }
            job.requests.push_back(std::move(request));
        }
        state.background->detailEpoch = state.columnEpoch;
        {
            ExclusiveSrwLock lock(state.background->directoryLock);
            state.background->detailJobs.push_back(std::move(job));
        }
        Check(EnsureProviderWorkers(state, TabBackground::Lane::Details), "size provider workers");
        Check(Eventually([&] {
            ReceiveDirectoryResults(state);
            for (int row = 0; row < state.total; ++row)
                if (!HasCachedColumnCell(state.entries[size_t(row)], PKEY_Size) ||
                    state.entries[size_t(row)].size != expected[size_t(row)]) return false;
            return true;
        }), "worker size text differs from native Shell");
        // Require the second pass to finish too, even if its text is identical.
        for (auto& entry : state.entries) entry.cells.clear();
        InvalidateDetailJobs(state);
    }
    const std::weak_ptr<TabBackground> retired = state.background;
    Stop(state.background);
    DestroyWindow(state.hwnd);
    Check(Eventually([&] { return retired.expired(); }), "size providers did not drain");
    folder.Reset(); item.Reset();
    // Delete only this named child of the shared private test fixture.
    Check(root.parent_path() == std::filesystem::path(directory), "size fixture cleanup boundary");
    Check(ReleasePrivateFixture(root), "size fixture cleanup");
    std::cout << "PASS: actual Shell Size text reaches background detail cells for path/PIDL identities, zero-byte/large files and blank folders\n";
}
