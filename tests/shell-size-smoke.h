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

// Sizes are never invented: Shell size-less item IDs (as Windows builds for
// removable media) cannot reach the Size column, unread files show "--",
// and Size sorts use the Shell's folder sizes.
static void TestShellSizeTruth(const std::wstring& directory) {
    const auto root = std::filesystem::path(directory) / L"shell-size-truth";
    std::filesystem::create_directories(root / L"small");
    std::filesystem::create_directories(root / L"large");
    const auto write = [](const std::filesystem::path& path, LONGLONG bytes) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "size truth fixture file");
        LARGE_INTEGER length{}; length.QuadPart = bytes;
        const bool sized = SetFilePointerEx(file, length, nullptr, FILE_BEGIN) && SetEndOfFile(file);
        CloseHandle(file);
        Check(sized, "size truth fixture length");
    };
    write(root / L"a.bin", 3001);
    write(root / L"b.bin", 70003);
    write(root / L"gone.bin", 4096);
    write(root / L"small" / L"one.bin", 10);
    write(root / L"large" / L"two.bin", 900000);
    ComPtr<IShellItem> item;
    ComPtr<IShellFolder2> folder;
    Check(SUCCEEDED(SHCreateItemFromParsingName(root.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
        SUCCEEDED(item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&folder))),
        "size truth Shell folder");
    UINT sizeColumn = 0;
    for (;; ++sizeColumn) {
        PROPERTYKEY key{};
        Check(sizeColumn < kMaxShellColumns &&
            SUCCEEDED(folder->MapColumnToSCID(sizeColumn, &key)), "size truth Size column");
        if (SameKey(key, PKEY_Size)) break;
    }
    // Independent expectations come from the folder's own parsed children.
    const auto native = [&](const std::wstring& leaf, std::wstring* text, ULONGLONG* value) {
        PIDLIST_RELATIVE child = nullptr;
        auto name = leaf;
        Check(SUCCEEDED(folder->ParseDisplayName(nullptr, nullptr, name.data(), nullptr, &child, nullptr)),
            "size truth native child");
        OwnedPidl owned(child);
        bool known = false;
        if (text) {
            SHELLDETAILS details{}; PWSTR formatted = nullptr;
            Check(SUCCEEDED(folder->GetDetailsOf(child, sizeColumn, &details)) &&
                SUCCEEDED(StrRetToStrW(&details.str, child, &formatted)), "size truth native text");
            *text = formatted ? formatted : L"";
            CoTaskMemFree(formatted);
        }
        if (value) {
            VARIANT raw{}; SHCOLUMNID column = PKEY_Size;
            known = SUCCEEDED(folder->GetDetailsEx(child, &column, &raw)) && raw.vt == VT_UI8;
            if (known) *value = raw.ullVal;
            VariantClear(&raw);
        }
        return known;
    };
    {
        // Witness: Windows does build item IDs without size data.
        const auto path = (root / L"a.bin").wstring();
        OwnedPidl simple(SHSimpleIDListFromPath(path.c_str()));
        ComPtr<IShellFolder2> parent; PCUITEMID_CHILD child = nullptr;
        VARIANT raw{}; SHCOLUMNID column = PKEY_Size;
        Check(simple && SUCCEEDED(SHBindToParent(simple.get(), IID_PPV_ARGS(&parent), &child)) &&
            SUCCEEDED(parent->GetDetailsEx(child, &column, &raw)) && raw.vt == VT_UI8 && raw.ullVal != 3001,
            "simple Shell IDs carried size data; removable-drive witness lost");
        VariantClear(&raw);
        WIN32_FILE_ATTRIBUTE_DATA data{};
        Check(GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data), "size truth attributes");
        ShellFindFacts facts{.attributes = data.dwFileAttributes, .sizeKnown = true, .size = 3001,
            .created = data.ftCreationTime, .accessed = data.ftLastAccessTime, .modified = data.ftLastWriteTime};
        std::wstring expected, text;
        native(L"a.bin", &expected, nullptr);
        ShellSizeBinding binding;
        Check(ReadShellSizeDetail(path, nullptr, text, binding, 1, &facts) && text == expected,
            "enumerated facts did not produce the Shell's own Size text");
    }
    ViewState state;
    state.hwnd = MessageWindow(); state.background->hwnd = state.hwnd;
    state.folderPath = root.wstring(); state.inScope = true; state.rowPx = 34;
    QueueDirectoryRefresh(state);
    Check(Eventually([&] { ReceiveDirectoryResults(state); return state.loaded && !state.directoryPending; }),
        "size truth names completion");
    Check(state.total == 5, "size truth row count");
    // A row without a size record whose item is unreadable shows "--", not 0 bytes.
    Check(DeleteFileW((root / L"gone.bin").c_str()), "size truth delete");
    DetailJob job;
    job.hwnd = state.hwnd; job.token = state.directoryToken; job.columnEpoch = state.columnEpoch;
    std::vector<std::wstring> expected(size_t(state.total));
    for (int row = 0; row < state.total; ++row) {
        const auto& entry = state.entries[size_t(row)];
        Check(entry.byteSizeKnown, "enumerated row lost its Win32 size record");
        if (entry.name == L"gone.bin") expected[size_t(row)] = L"--";
        else native(entry.name, &expected[size_t(row)], nullptr);
        DetailRequest request;
        request.index = row; request.path = entry.path; request.folder = entry.folder;
        // gone.bin stands for a native row with no size record whose item vanished.
        request.size = entry.byteSize; request.sizeKnown = entry.byteSizeKnown && entry.name != L"gone.bin";
        request.attributes = entry.attributes; request.modified = entry.lastWrite;
        request.created = entry.created; request.accessed = entry.accessed;
        request.missingKeys.push_back(PKEY_Size);
        job.requests.push_back(std::move(request));
    }
    state.background->detailEpoch = state.columnEpoch;
    {
        ExclusiveSrwLock lock(state.background->directoryLock);
        state.background->detailJobs.push_back(std::move(job));
    }
    Check(EnsureProviderWorkers(state, TabBackground::Lane::Details), "size truth provider workers");
    Check(Eventually([&] {
        ReceiveDirectoryResults(state);
        for (int row = 0; row < state.total; ++row)
            if (!HasCachedColumnCell(state.entries[size_t(row)], PKEY_Size) ||
                state.entries[size_t(row)].size != expected[size_t(row)]) return false;
        return true;
    }) || [&] {
        for (int row = 0; row < state.total; ++row)
            std::wcout << L"SIZE row=" << state.entries[size_t(row)].name << L" got=[" <<
                state.entries[size_t(row)].size << L"] expected=[" << expected[size_t(row)] << L"]\n";
        return false;
    }(), "Size cells differ from the Shell, or an unread size was shown as a value");
    // Size sorts order folders by the Shell's folder value, files by Win32.
    for (const auto placement : {FolderPlacement::ExplorerDefault, FolderPlacement::Mixed}) {
        auto sorted = state.entries;
        SortSpec size = DefaultSort(); size.explicitOrder = true; size.kind = SortKind::Size;
        size.key = PKEY_Size; size.placement = placement;
        std::map<std::wstring, std::wstring> types;
        SortDirectoryEntries(sorted, size, types, *state.background, state.directoryToken);
        ULONGLONG previousFolder = 0, previousFile = 0;
        for (const auto& entry : sorted) {
            if (entry.name == L"gone.bin") continue;
            ULONGLONG value = 0;
            const bool known = native(entry.name, nullptr, &value);
            if (entry.folder) {
                Check(entry.contentSizeKnown == known && (!known || entry.contentSize == value),
                    "folder sort value differs from the Shell's Size value");
                Check(value >= previousFolder, "folders not ordered by Shell size");
                previousFolder = value;
            } else {
                Check(value == entry.byteSize && value >= previousFile, "files not ordered by size");
                previousFile = value;
            }
        }
    }
    const std::weak_ptr<TabBackground> retired = state.background;
    Stop(state.background);
    DestroyWindow(state.hwnd);
    Check(Eventually([&] { return retired.expired(); }), "size truth providers did not drain");
    folder.Reset(); item.Reset();
    Check(root.parent_path() == std::filesystem::path(directory), "size truth cleanup boundary");
    Check(ReleasePrivateFixture(root), "size truth cleanup");
    std::cout << "PASS: Size never shows unread/size-less values, and sorts folders by Shell size\n";
}
