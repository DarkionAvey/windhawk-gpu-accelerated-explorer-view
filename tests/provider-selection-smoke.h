// Real Shell providers and a private offscreen ExplorerBrowser. No user files,
// foreground changes, clipboard access, popup tracking or invoked file verbs.
namespace ProviderSelectionFixture {
struct Member { std::string name, body; DWORD crc = 0, offset = 0; };
std::string ZipBytes(std::vector<Member> members) {
    std::vector<BYTE> bytes;
    const auto word = [&](unsigned value) { bytes.push_back(BYTE(value)); bytes.push_back(BYTE(value >> 8)); };
    const auto dword = [&](DWORD value) { word(value & 0xffff); word(value >> 16); };
    const auto text = [&](const std::string& value) { bytes.insert(bytes.end(), value.begin(), value.end()); };
    for (auto& member : members) {
        DWORD crc = ~0u;
        for (unsigned char byte : member.body) {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
        member.crc = ~crc; member.offset = static_cast<DWORD>(bytes.size());
        dword(0x04034b50); word(20); word(0x800); word(0); word(0); word(0x5021);
        dword(member.crc); dword(static_cast<DWORD>(member.body.size())); dword(static_cast<DWORD>(member.body.size()));
        word(static_cast<unsigned>(member.name.size())); word(0); text(member.name); text(member.body);
    }
    const DWORD central = static_cast<DWORD>(bytes.size());
    for (const auto& member : members) {
        dword(0x02014b50); word(20); word(20); word(0x800); word(0); word(0); word(0x5021);
        dword(member.crc); dword(static_cast<DWORD>(member.body.size())); dword(static_cast<DWORD>(member.body.size()));
        word(static_cast<unsigned>(member.name.size())); word(0); word(0); word(0); word(0);
        dword(member.name.ends_with('/') ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE);
        dword(member.offset); text(member.name);
    }
    const DWORD centralSize = static_cast<DWORD>(bytes.size()) - central;
    dword(0x06054b50); word(0); word(0); word(static_cast<unsigned>(members.size()));
    word(static_cast<unsigned>(members.size())); dword(centralSize); dword(central); word(0);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
void WriteZip(const std::filesystem::path& path, const std::string& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, 0, nullptr);
    DWORD written = 0;
    Check(file != INVALID_HANDLE_VALUE, "private archive creation");
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    Check(ok && written == bytes.size(), "private archive bytes");
}
void Zip(const std::filesystem::path& path) {
    std::vector<Member> members;
    for (int i = 0; i < 96; ++i) members.push_back({"file-" + std::to_string(i) + ".txt", "private archive fixture"});
    members.push_back({"one/", ""}); members.push_back({"one/duplicate.txt", "one"});
    members.push_back({"two/", ""}); members.push_back({"two/duplicate.txt", "two"});
    members.push_back({"caf\xc3\xa9.txt", "unicode"});
    WriteZip(path, ZipBytes(std::move(members)));
}

void DiskAndAlias(const std::filesystem::path& target, const std::filesystem::path& alias) {
    std::filesystem::create_directory(target);
    const auto write = [](const std::filesystem::path& path, const std::string& contents) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, 0, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "private provider file");
        DWORD written = 0;
        const bool ok = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr);
        CloseHandle(file);
        Check(ok && written == contents.size(), "private provider bytes");
    };
    for (int i = 0; i < 96; ++i) write(target / (L"file-" + std::to_wstring(i) + L".txt"), "private disk fixture");
    write(target / L"caf\u00e9.txt", "unicode");
    for (const auto* name : {L"one", L"two"}) {
        std::filesystem::create_directory(target / name);
        write(target / name / L"duplicate.txt", "duplicate");
    }
    std::filesystem::create_directory(alias);
    write(alias / L"desktop.ini", "[.ShellClassInfo]\r\nCLSID2={0AFACED1-E828-11D1-9187-B532F1E9575D}\r\nFlags=2\r\n");
    Check(SetFileAttributesW((alias / L"desktop.ini").c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM),
        "private delegated provider configuration");
    ComPtr<IShellLinkW> link; ComPtr<IPersistFile> persist;
    Check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) &&
        SUCCEEDED(link->SetPath(target.c_str())) && SUCCEEDED(link.As(&persist)) &&
        SUCCEEDED(persist->Save((alias / L"target.lnk").c_str(), TRUE)) &&
        SetFileAttributesW(alias.c_str(), FILE_ATTRIBUTE_READONLY), "private delegated provider target");
}

struct Browser {
    ViewState state;
    HWND frame = nullptr;
    ComPtr<IExplorerBrowser> browser;
    NativeViewportEvents* sink = nullptr;
    DWORD cookie = 0;
    unsigned shellHints = 0;
    Browser() {
        frame = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"private provider selection",
            WS_POPUP | WS_VISIBLE, -32000, -32000, 1000, 700, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        state.hwnd = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 1000, 700,
            frame, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(frame && state.hwnd, "provider selection windows");
        state.frameHwnd = state.activeTabHost = frame; state.background->hwnd = state.hwnd;
        state.visible = state.inScope = state.detailsViewActive = true;
        state.firstContentSubmittedAt = 1; state.deferColumnProviders = false;
        SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
        Check(SetWindowSubclass(state.hwnd, +[](HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)->LRESULT {
            if (message == kMsgShellItemCreated) {
                ++reinterpret_cast<Browser*>(data)->shellHints;
                return OverlayProc(hwnd, message, wp, lp);
            }
            return DefSubclassProc(hwnd, message, wp, lp);
        }, 1, reinterpret_cast<DWORD_PTR>(this)), "provider Shell notification receiver");
        Check(SUCCEEDED(CoCreateInstance(CLSID_ExplorerBrowser, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&browser))), "provider ExplorerBrowser");
        RECT bounds{0, 0, 1000, 700}; FOLDERSETTINGS settings{FVM_DETAILS, FWF_NOBROWSERVIEWSTATE};
        Check(SUCCEEDED(browser->Initialize(frame, &bounds, &settings)), "provider browser initialization");
        browser->SetOptions(EBO_NOBORDER | EBO_NOTRAVELLOG);
        sink = new NativeViewportEvents(state, true, true);
        Check(SUCCEEDED(browser->Advise(sink, &cookie)), "provider browser events");
    }
    void Pump() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.hwnd == state.hwnd && (message.message == kMsgPreparedSelection ||
                message.message == kMsgFlushDirectoryChanges)) {
                OverlayProc(state.hwnd, message.message, message.wParam, message.lParam);
            }
            else { TranslateMessage(&message); DispatchMessageW(&message); }
        }
    }
    void Navigate(const std::wstring& path, int expected) {
        PIDLIST_ABSOLUTE raw = nullptr;
        Check(SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &raw, 0, nullptr)), "provider navigation PIDL");
        OwnedPidl identity(raw);
        NavigateIdentity(identity.get(), expected);
    }
    void NavigateIdentity(PCIDLIST_ABSOLUTE identity, int expected) {
        sink->navigationComplete = false;
        Check(SUCCEEDED(browser->BrowseToIDList(identity, SBSP_ABSOLUTE)), "provider browser navigation");
        Bind(expected);
    }
    void Bind(int expected) {
        Check(Eventually([&] {
            Pump(); int count = -1;
            return sink->navigationComplete && sink->view &&
                SUCCEEDED(sink->view->ItemCount(SVGIO_ALLVIEW, &count)) && count == expected;
        }, 30000), "provider native model population");
        state.folderView = sink->view;
        state.columnManager.Reset(); state.folderView.As(&state.columnManager);
        browser.As(&state.shellBrowser);
        ComPtr<IServiceProvider> services;
        if (!state.shellBrowser && SUCCEEDED(state.folderView.As(&services)))
            services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&state.shellBrowser));
        ComPtr<IObjectWithSite> site;
        if (!state.shellBrowser && SUCCEEDED(state.folderView.As(&site)))
            site->GetSite(IID_PPV_ARGS(&state.shellBrowser));
        if (!state.shellBrowser && SUCCEEDED(browser.As(&services)))
            services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&state.shellBrowser));
        Check(bool(state.shellBrowser), "provider native browser site");
        std::wstring location;
        ComPtr<IShellFolder> folder;
        Check(GetFolderPath(state.folderView.Get(), location, folder) && IsSupportedLocation(location),
            "native provider destination was rejected as an unsupported location");
        ResetForNewDirectory(state, location, folder, false, state.activeTabHost);
        state.deferColumnProviders = false; UpdateMetrics(state);
        auto result = WaitForComplete(*state.background, state.directoryToken);
        Check(result.error == ERROR_SUCCESS && result.entries.size() == size_t(expected), "provider GPU model population");
        PublishDirectoryResult(*state.background, std::move(result)); ReceiveDirectoryResults(state);
        Check(Eventually([&] {
            Pump(); ReceiveDirectoryResults(state);
            return state.directoryWatch && !state.directoryPending && !state.namespaceCheckPending &&
                !state.watchedChangePending && state.loaded && state.total == expected;
        }, 30000), "provider watch/model did not settle after navigation");
    }
    ~Browser() {
        CancelNativeSelectionPreparation(state); CloseDirectoryWatch(state);
        UnregisterAnimationWindow(state.hwnd);
        state.validatedFolderStamp.reset();
        RestoreNativeContent(state); state.folderView.Reset(); state.folder.Reset(); state.shellBrowser.Reset();
        if (browser) { browser->Unadvise(cookie); browser->Destroy(); }
        if (sink) { sink->view.Reset(); sink->Release(); }
        browser.Reset(); Stop(state.background);
        SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, 0); DestroyWindow(frame);
    }
};
}

static void TestProviderSelection(const std::wstring& fixture) {
    const auto root = std::filesystem::path(fixture) / L"provider-selection";
    std::filesystem::create_directory(root);
    const auto archive = root / L"private.zip";
    const auto target = root / L"target";
    const auto alias = root / L"alias";
    ProviderSelectionFixture::Zip(archive);
    ProviderSelectionFixture::DiskAndAlias(target, alias);
    for (const auto& path : {archive, alias, target}) {
        ProviderSelectionFixture::Browser host;
        auto& state = host.state;
        FocusFixture::Session focus(host.frame, state.hwnd);
        host.Navigate(path.wstring(), 99);
        const bool provider = path != target;
        Check(bool(state.namespaceIdentities) == provider &&
            bool(state.entries.front().shellIdentity) == provider, "provider row identity authority");
        const auto generation = state.directoryToken;
        const auto await = [&] {
            Check(Eventually([&] { host.Pump(); return !state.pendingNativeSelection; }, 30000),
                "provider selection preparation stalled");
        };
        const auto verify = [&](const std::vector<int>& rows) {
            int count = -1; state.folderView->ItemCount(SVGIO_SELECTION, &count);
            Check(count == int(rows.size()) && !state.nativeSelectionUnsafe && !state.nativeSelectionRecoveryPending &&
                !state.nativeSelectionImportPending && !state.directoryPending && state.directoryToken == generation,
                "provider selection echo became unsafe or refreshed the item list");
            for (int i = 0; i < state.total; ++i)
                Check(bool(state.selectedFlags[i]) == (std::find(rows.begin(), rows.end(), i) != rows.end()),
                    "provider native echo selected a different GPU item");
            if (!rows.empty()) Check(state.selected == rows.back(), "provider focused child did not map to its GPU row");
        };
        const auto echo = [&](const std::vector<int>& rows) {
            const bool captured = CaptureNativeSelection(state, true);
            if (!captured) {
                int actual = -1; state.folderView->ItemCount(SVGIO_SELECTION, &actual);
                std::cout << "PROVIDER CAPTURE kind=" << (path == archive ? "archive" : provider ? "delegated" : "ordinary")
                    << " expected=" << rows.size() << " actual=" << actual << " mode=" << state.layoutNativeMode
                    << " unsafe=" << state.nativeSelectionUnsafe << " recovery=" << state.nativeSelectionRecoveryPending
                    << " restore=" << NativeSelectionRestorePending(state) << " command=" << state.nativeFileCommandDepth
                    << " pending=" << state.pendingNativeSelection << std::endl;
            }
            Check(captured, "provider selection echo capture failed"); await(); verify(rows);
        };
        const auto commit = [&](const std::vector<int>& rows) {
            state.selectedFlags.Fill(0);
            for (int row : rows) state.selectedFlags[row] = 1;
            state.selected = state.anchor = rows.empty() ? -1 : rows.back();
            Check(SyncNativeSelection(state), "provider GPU-to-native selection failed"); await();
            echo(rows);
        };
        const auto menu = [&](int row) {
            MenuFixture::Session tracking(state.hwnd);
            ContextMenu(state, row, POINT{0, 0}); await();
            if (MenuFixture::calls != 1 || MenuFixture::items <= 0 || state.nativeSelectionRecoveryPending)
                std::cout << "PROVIDER MENU kind=" << (path == archive ? "archive" : provider ? "delegated" : "ordinary")
                    << " row=" << row << " selected=" << std::count(state.selectedFlags.begin(), state.selectedFlags.end(), 1)
                    << " mode=" << state.layoutNativeMode << " calls=" << MenuFixture::calls << " items=" << MenuFixture::items
                    << " unsafe=" << state.nativeSelectionUnsafe << " pending=" << state.pendingNativeSelection
                    << " recovery=" << state.nativeSelectionRecoveryPending << std::endl;
            Check(MenuFixture::calls == 1 && MenuFixture::items > 0 && !state.nativeSelectionRecoveryPending,
                "GPU context menu was blocked after the provider selection echo");
        };
        const auto external = [&](const std::vector<int>& rows) {
            Check(ClearNativeSelection(state), "provider external deselection");
            for (int row : rows) {
                OwnedPidl child;
                PITEMID_CHILD raw = nullptr;
                Check(SUCCEEDED(EntryChildPidl(state, state.entries[row], &raw)) && raw, "external provider child");
                child.reset(raw);
                PCUITEMID_CHILD children[]{child.get()};
                Check(SUCCEEDED(state.folderView->SelectAndPositionItems(1, children, nullptr,
                    SVSI_SELECT | SVSI_FOCUSED | SVSI_NOTAKEFOCUS)), "external provider selection");
            }
            echo(rows);
        };
        int unicode = -1, folder = -1;
        for (int i = 0; i < state.total; ++i) {
            if (state.entries[i].name.starts_with(L"caf")) unicode = i;
            if (state.entries[i].folder) folder = i;
        }
        Check(unicode >= 0 && folder >= 0, "provider Unicode/folder fixtures");
        for (const auto mode : {FVM_DETAILS, FVM_LIST, FVM_ICON}) {
            Check(SUCCEEDED(state.folderView->SetViewModeAndIconSize(mode, 48)), "provider native view mode");
            host.Pump();
            state.rtlLayout = mode == FVM_LIST;
            state.gridView = mode == FVM_ICON; state.columnsView = mode == FVM_DETAILS;
            state.layoutNativeMode = mode; UpdateMetrics(state);
            commit({0}); menu(0);
            for (int repeat = 0; repeat < 3; ++repeat) echo({0});
            if (provider) {
                Check(CaptureNativeSelection(state, true) && state.pendingNativeSelection,
                    "provider context menu preparation fixture");
                menu(0); verify({0});
            }
            commit({unicode}); menu(unicode);
            commit({folder}); menu(folder);
            commit({0, 1}); menu(1);
            external({2, 3}); menu(3);
            std::vector<int> bulk, changed;
            for (int i = 0; i < 70; ++i) { bulk.push_back(i); changed.push_back(i + 20); }
            commit(bulk); menu(bulk.back());
            external(changed); menu(changed.back());
            external({});
        }
        std::cout << "PROVIDER SELECTION kind=" << (path == archive ? "archive" : provider ? "delegated filesystem" : "ordinary filesystem")
            << " rows=" << state.total << " modes=Details/List/grid single/multiple/70-item external/echo/menu passed\n";
    }
    SetFileAttributesW(alias.c_str(), FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW((alias / L"desktop.ini").c_str(), FILE_ATTRIBUTE_NORMAL);
    Check(std::filesystem::weakly_canonical(root).parent_path() ==
        std::filesystem::weakly_canonical(std::filesystem::path(fixture)), "provider cleanup escaped its private fixture");
    Check(Eventually([&] {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return !error;
    }), "provider selection fixture cleanup did not settle");
    std::cout << "PASS: real provider and filesystem selection/focus, repeated echoes, external selection, Unicode, folders, bulk groups and GPU context menus; no popup or commands\n";
}
