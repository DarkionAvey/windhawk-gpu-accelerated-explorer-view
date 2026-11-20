// Regressions for the 1.0.12 (high) and 1.0.13 (medium) review fixes; see
// docs/1.0.10-code-review-vs-1.0.4.md. Private fixtures and real Windows APIs
// only: Shell schema, known folders, COM apartments, SRW locks, USER queues.

// H1: Content's aside labels are the property system's own display names,
// created once and shared read-only by every paint lane.
static void TestContentAsideLabels(const std::wstring&) {
    WorkerComApartment apartment;
    const auto independent = [](REFPROPERTYKEY key) {
        ComPtr<IPropertyDescription> description; PWSTR text = nullptr; std::wstring label;
        if (SUCCEEDED(PSGetPropertyDescription(key, IID_PPV_ARGS(&description))) &&
            SUCCEEDED(description->GetDisplayName(&text)) && text) label = text;
        CoTaskMemFree(text);
        return label;
    };
    const std::wstring date = independent(PKEY_DateModified), size = independent(PKEY_Size);
    Check(!date.empty() && !size.empty(), "PRECONDITION: property schema display names");
    // Concurrent first use: one initialization, one immutable object for all.
    std::vector<const std::array<std::wstring, 2>*> seen(8, nullptr);
    std::vector<std::thread> lanes;
    for (size_t i = 0; i < seen.size(); ++i)
        lanes.emplace_back([&seen, i] { WorkerComApartment lane; seen[i] = &ContentAsideLabels(); });
    for (auto& lane : lanes) lane.join();
    Check(std::all_of(seen.begin(), seen.end(), [&](const auto* labels) { return labels == seen.front(); }),
        "paint lanes received different label objects");
    Check((*seen.front())[0] == date && (*seen.front())[1] == size,
        "Content labels differ from the property system's display names");
    std::cout << "PASS: Content aside labels are the schema display names, initialized once across threads\n";
}

// H2: exact item lists are listed; inferred namespaces stay native.
static void TestFolderScopeExactLists(const std::wstring& fixture) {
    WorkerComApartment apartment;
    for (const auto* known : {&FOLDERID_AppsFolder, &FOLDERID_PrintersFolder}) {
        PIDLIST_ABSOLUTE raw = nullptr;
        Check(SUCCEEDED(SHGetKnownFolderIDList(*known, 0, nullptr, &raw)) && raw, "read-only known folder");
        OwnedPidl folder(raw);
        Check(UsesNamespaceFolderIdentity(FolderLocation(folder.get())), "an exact item list was not listed");
    }
    PIDLIST_ABSOLUTE panel = nullptr;
    Check(SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_ControlPanelFolder, 0, nullptr, &panel)) && panel,
        "read-only Control Panel identity");
    OwnedPidl ownedPanel(panel);
    Check(!IsSupportedLocation(FolderLocation(panel)), "Control Panel left the native view");
    OwnedPidl directory(SHSimpleIDListFromPath(fixture.c_str()));
    Check(directory && FolderLocation(directory.get()) == fixture, "a filesystem folder lost its Win32 path");
    std::cout << "PASS: Applications and Printers by identity, Control Panel native, filesystem paths unchanged\n";
}

// H3: diagnostics that query Shell state run only with logging enabled.
static void TestDiagnosticsGate(const std::wstring&) {
    struct Restore { bool perf, tracing; ~Restore() { g_perf = perf; g_pipelineTracing = tracing; } }
        restore{g_perf.exchange(false), g_pipelineTracing.exchange(false)};
    Check(!DiagnosticsEnabled(), "diagnostics enabled with logging off");
    ViewState state;
    // A disabled probe must not touch the view at all: a null view proves it.
    ProbeShellSortState(state, nullptr, L"disabled", {});
    g_pipelineTracing = true;
    Check(DiagnosticsEnabled(), "pipeline tracing did not enable diagnostics");
    g_pipelineTracing = false; g_perf = true;
    Check(DiagnosticsEnabled(), "logging did not enable diagnostics");
    std::cout << "PASS: Shell sort diagnostics and their queries follow the logging option\n";
}

// H4: a failed multiple rename is posted, never shown inside the transaction,
// and its text is Windows' own localized rename failure message.
static void TestRenameFailureNotice(const std::wstring&) {
    ViewState state;
    state.hwnd = CreateWindowExW(0, L"STATIC", L"rename-notice", 0, 0, 0, 0, 0, HWND_MESSAGE,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(state.hwnd != nullptr, "private message window");
    struct Cleanup { HWND hwnd; ~Cleanup() { DestroyWindow(hwnd); } } cleanup{state.hwnd};
    state.token = 41;
    const std::wstring name = L"Renamed (1).txt";
    const HRESULT hr = HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    ReportMultipleRenameFailure(state, L"regression", name, hr);
    MSG queued{};
    Check(PeekMessageW(&queued, state.hwnd, kMsgRenameFailureNotice, kMsgRenameFailureNotice, PM_REMOVE) &&
        queued.lParam == static_cast<LPARAM>(state.token), "failure notice was not deferred to its own dispatch");
    Check(state.renameFailureName == name && state.renameFailureResult == hr, "failure notice lost its item");
    PWSTR system = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&system), 0, nullptr);
    std::wstring cause = system ? system : L"";
    LocalFree(system);
    while (!cause.empty() && iswspace(cause.back())) cause.pop_back();
    const std::wstring text = FormatRenameFailureNotice(name, hr);
    Check(!text.empty(), "PRECONDITION: shell32 rename failure resource");
    Check(text.find(name) != std::wstring::npos && text.find(cause) != std::wstring::npos,
        "notice is not the localized message for this item and error");
    Check(FormatRenameFailureNotice({}, hr).empty(), "a notice without an item was composed");
    std::cout << "PASS: multiple-rename failures post Windows' localized rename message after the transaction\n";
}

// M1: only listings that show status glyphs re-query properties on an icon change.
static void TestIconInvalidationScope(const std::wstring&) {
    ViewState state;
    state.entries.resize(3);
    for (auto& entry : state.entries) entry.cells.push_back({PKEY_ItemTypeText, L"Text"});
    const auto before = state.propertyEpoch;
    InvalidateShellIcons(state);
    Check(state.propertyEpoch == before, "an icon change re-queried text-only properties");
    state.entries[1].cells.push_back({PKEY_Size, L"", std::make_shared<const std::vector<IconPixels>>()});
    InvalidateShellIcons(state);
    Check(state.propertyEpoch == before + 1, "status glyphs were not refreshed after an icon change");
    std::cout << "PASS: Shell icon invalidation refreshes status glyph cells only where they exist\n";
}

// M2: nested changes refresh a row only where folder sizes are shown or order rows.
static void TestFolderSizeUse(const std::wstring&) {
    ViewState state;
    Check(!FolderSizesUsed(state), "an unsorted list without a Size column used folder sizes");
    state.columnsView = true;
    state.columns.push_back({}); state.columns.back().key = PKEY_DateModified;
    Check(!FolderSizesUsed(state), "a Date column used folder sizes");
    state.columns.push_back({}); state.columns.back().key = PKEY_Size;
    Check(FolderSizesUsed(state), "a visible Size column ignored folder sizes");
    state.columns.pop_back();
    state.sort.explicitOrder = true; state.sort.kind = SortKind::Size; state.sort.key = PKEY_Size;
    Check(FolderSizesUsed(state), "a Size sort ignored folder sizes");
    state.sort = {};
    state.grouping.enabled = true; state.grouping.key = PKEY_Size;
    Check(FolderSizesUsed(state), "Size grouping ignored folder sizes");
    std::cout << "PASS: nested folder changes are scoped to views that show or order by folder size\n";
}

// M3: recorded sizes are read under a shared lock; obsolete provider work stops.
static void TestFolderSizeLanes(const std::wstring& fixture) {
    ViewState owner;
    auto& work = owner.background; // The tab's own lanes and lock.
    const std::wstring listed = fixture + L"\\listed", other = fixture + L"\\other";
    for (const auto& folder : {listed, other, listed + L"\\a", other + L"\\a"})
        Check(CreateDirectoryW(folder.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS, "private folder");
    work->folderSizeFolder = listed;
    work->folderSizes[listed + L"\\a"] = 4096;
    std::vector<Entry> entries(2);
    entries[0].path = listed + L"\\a"; entries[0].folder = true;
    entries[1].path = other + L"\\a"; entries[1].folder = true;
    // A concurrent reader (another lane) must not block the names lane.
    HANDLE held = CreateEventW(nullptr, TRUE, FALSE, nullptr), release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(held && release, "lock fixture events");
    std::thread reader([&] { SharedSrwLock lock(work->directoryLock); SetEvent(held); WaitForSingleObject(release, 30000); });
    Check(WaitForSingleObject(held, 10000) == WAIT_OBJECT_0, "concurrent reader");
    std::atomic<bool> done{false};
    std::thread names([&] { ResolveFolderContentSizes(entries, *work, 0, FolderSizeSource::Recorded); done = true; });
    const bool shared = Eventually([&] { return done.load(); }, 10000);
    SetEvent(release); reader.join(); names.join(); CloseHandle(held); CloseHandle(release);
    Check(shared, "recorded folder sizes waited for another reader");
    Check(entries[0].contentSizeKnown && entries[0].contentSize == 4096, "recorded folder size not applied");
    Check(!entries[1].contentSizeKnown, "a size recorded for another folder was applied");
    // An obsolete generation asks no provider and records nothing.
    work->directoryGeneration = 7;
    std::vector<Entry> stale(1);
    stale[0].path = other + L"\\a"; stale[0].folder = true;
    {
        WorkerComApartment apartment;
        ResolveFolderContentSizes(stale, *work, 6, FolderSizeSource::Fresh);
    }
    Check(!stale[0].contentSizeKnown && work->folderSizeFolder == listed && work->folderSizes.size() == 1,
        "an obsolete folder-size request still queried the provider");
    std::cout << "PASS: folder sizes read under a shared lock and obsolete provider requests stop\n";
}

// M4: replacing a folder in place ends every interaction bound to its rows.
static void TestInPlaceReplacementInteraction(const std::wstring&) {
    ViewState state;
    state.hwnd = CreateWindowExW(0, L"STATIC", L"in-place", 0, 0, 0, 0, 0, HWND_MESSAGE,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(state.hwnd != nullptr, "private message window");
    struct Cleanup { HWND hwnd; ~Cleanup() { DestroyWindow(hwnd); } } cleanup{state.hwnd};
    state.token = 9;
    state.clickRename.armed = true; state.clickRename.row = 2;
    state.nativeRenameHold = 9; state.nativeRenameText = L"held"; state.nativeRenameToken = 9;
    state.renameFailureName = L"old"; state.typeSearch = L"ab";
    state.shellDragRow = 1; state.shellDragItem = L"C:\\old\\item";
    const auto epoch = state.mouseFolderEpoch;
    RetireListingInteraction(state);
    Check(!state.clickRename.armed && !state.nativeRenameHold && state.nativeRenameText.empty() &&
        state.renameFailureName.empty(), "rename work from the replaced listing survived");
    Check(state.typeSearch.empty() && state.shellDragRow == -1 && state.shellDragItem.empty(),
        "pointer or type-to-select state from the replaced listing survived");
    Check(state.mouseFolderEpoch != epoch, "clicks in the replaced listing could pair with new rows");
    std::cout << "PASS: in-place folder replacement retires rename, click, drag and type-to-select state\n";
}

// M5: an IME composition keeps the EDIT's own text, caret and pointer mapping.
static void TestRenameImeComposition(const std::wstring&) {
    ViewState state;
    HWND edit = CreateWindowExW(0, L"EDIT", L"name", WS_POPUP, 0, 0, 100, 20, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    Check(edit != nullptr, "private EDIT");
    struct Cleanup { HWND hwnd; ~Cleanup() { DestroyWindow(hwnd); } } cleanup{edit};
    state.rename.editor = edit;
    state.rename.imeComposing = true;
    Check(!BuildRenameTextLines(state, edit) && state.rename.textFallback &&
        std::string_view(state.rename.textFallback) == "IME composition" && state.rename.textLines.empty(),
        "list-matched text replaced an IME composition");
    std::cout << "PASS: rename field keeps native drawing during IME composition\n";
}

// M7: the New menu hook never targets COM marshaling code.
static void TestNewMenuHookTarget(const std::wstring&) {
    void* fromMta = reinterpret_cast<void*>(1);
    std::thread mta([&] {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        fromMta = NewMenuInvokeCommandAddress();
        if (SUCCEEDED(hr)) CoUninitialize();
    });
    mta.join();
    Check(fromMta == nullptr, "an MTA thread resolved a hook target");
    void* fromSta = nullptr;
    std::thread sta([&] { fromSta = NewMenuInvokeCommandAddress(); });
    sta.join();
    Check(fromSta != nullptr, "PRECONDITION: Shell New menu object");
    HMODULE owner = nullptr; wchar_t name[MAX_PATH]{};
    Check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        static_cast<LPCWSTR>(fromSta), &owner) && GetModuleFileNameW(owner, name, ARRAYSIZE(name)),
        "hook target module");
    const std::wstring_view leaf(PathFindFileNameW(name));
    for (const wchar_t* proxy : {L"combase.dll", L"ole32.dll", L"oleaut32.dll", L"rpcrt4.dll"})
        Check(_wcsicmp(leaf.data(), proxy) != 0, "hook target is COM marshaling code");
    std::cout << "PASS: New menu hook resolves only a direct Shell object, never from an MTA\n";
}

// M9: unthemed drawing does not rebuild an empty contrast cache per item.
static void TestContrastCacheRelease(const std::wstring&) {
    ThemeContrastResources<6> cache;
    cache.accent = RGB(1, 2, 3);
    ReleaseContrastSurfaces(cache);
    Check(cache.accent == RGB(1, 2, 3), "an empty contrast cache was rebuilt");
    cache.snapshot = std::make_shared<const Theme::Document>();
    ReleaseContrastSurfaces(cache);
    Check(!cache.snapshot && cache.accent == CLR_INVALID, "themed contrast resources were retained");
    std::cout << "PASS: contrast resources are released only when a themed frame created them\n";
}
