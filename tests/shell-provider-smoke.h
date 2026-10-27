// An ordinary private folder can delegate its contents through the Shell.
// No user locations, native Explorer input, network shares or clipboard access.
static void TestShellProviderContents(const std::wstring& fixture) {
    const auto root = std::filesystem::path(fixture) / L"shell-provider";
    const auto target = root / L"contents";
    const auto alias = root / L"alias";
    std::filesystem::create_directories(target / L"child folder");
    std::filesystem::create_directory(alias);
    const auto write = [](const std::filesystem::path& path, const std::string& bytes) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written = 0;
        Check(file != INVALID_HANDLE_VALUE, "provider fixture file");
        const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
        CloseHandle(file);
        Check(ok && written == bytes.size(), "provider fixture contents");
    };
    write(target / L"visible.txt", "target contents");
    write(target / L"hidden.txt", "hidden target");
    Check(SetFileAttributesW((target / L"hidden.txt").c_str(), FILE_ATTRIBUTE_HIDDEN), "provider hidden fixture");
    // This is a standard folder-shortcut provider, placed outside every known
    // folder. The production implementation must not recognize this CLSID/path.
    write(alias / L"desktop.ini", "[.ShellClassInfo]\r\nCLSID2={0AFACED1-E828-11D1-9187-B532F1E9575D}\r\nFlags=2\r\n");
    Check(SetFileAttributesW((alias / L"desktop.ini").c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM),
        "provider desktop.ini attributes");
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> persist;
    Check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) &&
        SUCCEEDED(link->SetPath(target.c_str())) && SUCCEEDED(link.As(&persist)) &&
        SUCCEEDED(persist->Save((alias / L"target.lnk").c_str(), TRUE)), "provider target link");
    persist.Reset(); link.Reset();
    Check(SetFileAttributesW(alias.c_str(), FILE_ATTRIBUTE_READONLY), "provider alias attributes");

    {
        ViewState ordinary;
        ordinary.hwnd = IconTestWindow(); ordinary.background->hwnd = ordinary.hwnd;
        ordinary.folderPath = target.wstring(); ordinary.inScope = true;
        QueueDirectoryRefresh(ordinary);
        const auto result = WaitForComplete(*ordinary.background, ordinary.directoryToken);
        Check(result.error == ERROR_SUCCESS && result.entries.size() == 2 &&
            std::none_of(result.entries.begin(), result.entries.end(), [](const Entry& e) { return bool(e.shellIdentity); }),
            "ordinary directory lost its filesystem fast path");
        Stop(ordinary.background); DestroyWindow(ordinary.hwnd);
    }
    ViewState state;
    state.hwnd = IconTestWindow(); state.background->hwnd = state.hwnd;
    state.folderPath = alias.wstring(); state.inScope = state.visible = true;
    UpdateMetrics(state);
    const auto refresh = [&] {
        QueueDirectoryRefresh(state, true);
        auto result = WaitForComplete(*state.background, state.directoryToken);
        Check(result.error == ERROR_SUCCESS, "Shell provider enumeration failed");
        PublishDirectoryResult(*state.background, std::move(result));
        ReceiveDirectoryResults(state);
    };
    const auto expected = [&] {
        ComPtr<IShellItem> item; ComPtr<IShellFolder> folder; ComPtr<IEnumIDList> enumeration;
        Check(SUCCEEDED(SHCreateItemFromParsingName(alias.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
            SUCCEEDED(item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&folder))), "native provider binding");
        SHCONTF flags = SHCONTF_FOLDERS | SHCONTF_NONFOLDERS;
        if (state.visibility.showHidden) flags |= SHCONTF_INCLUDEHIDDEN;
        Check(SUCCEEDED(folder->EnumObjects(nullptr, flags, &enumeration)), "native provider enumeration");
        std::set<std::wstring> paths;
        while (enumeration) {
            PITEMID_CHILD raw = nullptr; ULONG fetched = 0;
            if (enumeration->Next(1, &raw, &fetched) != S_OK || !fetched) break;
            OwnedPidl child(raw);
            ComPtr<IShellItem> row; PWSTR text = nullptr;
            Check(SUCCEEDED(SHCreateItemWithParent(nullptr, folder.Get(), child.get(), IID_PPV_ARGS(&row))) &&
                SUCCEEDED(row->GetDisplayName(SIGDN_FILESYSPATH, &text)), "native provider row path");
            paths.emplace(text); CoTaskMemFree(text);
        }
        return paths;
    };
    const auto compare = [&] {
        std::set<std::wstring> paths;
        for (const auto& entry : state.entries) paths.emplace(entry.path);
        Check(paths == expected(), "GPU enumerated the physical alias instead of the Shell provider's items");
    };
    refresh(); compare();
    Check(state.total == 2, "provider initial membership");
    const int file = GpuRowForNativePath(state, (target / L"visible.txt").wstring());
    const int folder = GpuRowForNativePath(state, (target / L"child folder").wstring());
    Check(file >= 0 && folder >= 0 && !state.entries[file].folder && state.entries[folder].folder &&
        state.entries[file].byteSize == 15 && state.entries[file].shellIdentity && state.entries[file].shellChild,
        "provider lost file/folder metadata or native command identities");
    state.selectedFlags.Fill(0); state.selectedFlags[file] = 1; state.selected = file;
    state.visibility.showHidden = true;
    state.sort.explicitOrder = true; state.sort.kind = SortKind::Name; state.sort.key = PKEY_FileName;
    refresh(); compare();
    Check(state.total == 3 && state.selected >= 0 && state.entries[state.selected].path == (target / L"visible.txt").wstring(),
        "provider refresh lost selection or visibility policy");
    WatchDirectory(state);
    Check(Eventually([&] { ReceiveDirectoryResults(state); return state.dirChange != INVALID_HANDLE_VALUE; }),
        "provider target watch setup");
    write(target / L"created.txt", "new target item");
    Check(WaitForSingleObject(state.dirChange, 10000) == WAIT_OBJECT_0, "provider watched the alias instead of target changes");
    refresh(); compare();
    for (const auto& path : {target / L"visible.txt", target / L"hidden.txt", target / L"created.txt"})
        Check(DeleteFileW(path.c_str()), "provider fixture deletion");
    std::filesystem::remove(target / L"child folder");
    refresh(); compare();
    Check(state.total == 0, "empty provider target exposed alias internals");
    CloseDirectoryWatch(state);
    state.validatedFolderStamp.reset();
    Stop(state.background); DestroyWindow(state.hwnd);
    SetFileAttributesW(alias.c_str(), FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW((alias / L"desktop.ini").c_str(), FILE_ATTRIBUTE_NORMAL);
    Check(Eventually([&] {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return !error;
    }), "provider fixture cleanup did not settle after Shell release");
    std::cout << "PASS: Shell provider membership, actual paths/metadata, selection, sorted refresh, hidden/empty targets and target change watching\n";
}
