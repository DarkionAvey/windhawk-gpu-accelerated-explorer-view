// Shell namespace identity and search-results highlighting. Uses a private
// fixture folder; system folders are read-only probes found by Windows APIs.

// Windows gives one item differently-encoded IDs (a simple ID and its
// enumerated ID differ in bytes yet compare canonically equal). A relisted
// namespace folder must keep the identity its rows already had.
static void TestNamespaceIdentityStability(const std::wstring& directory) {
    const auto root = std::filesystem::path(directory) / L"namespace-identity";
    std::filesystem::create_directories(root);
    for (const auto& [name, bytes] : {std::pair{L"kept.bin", 123457LL}, std::pair{L"added.bin", 77LL}}) {
        HANDLE file = CreateFileW((root / name).c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "namespace identity fixture file");
        LARGE_INTEGER length{}; length.QuadPart = bytes;
        const bool sized = SetFilePointerEx(file, length, nullptr, FILE_BEGIN) && SetEndOfFile(file);
        CloseHandle(file);
        Check(sized, "namespace identity fixture length");
    }
    PIDLIST_ABSOLUTE raw = nullptr;
    Check(SUCCEEDED(SHParseDisplayName(root.c_str(), nullptr, &raw, 0, nullptr)), "namespace identity folder ID");
    OwnedPidl parent(raw);
    ComPtr<IShellFolder2> folder;
    Check(SUCCEEDED(SHBindToObject(nullptr, parent.get(), nullptr, IID_PPV_ARGS(&folder))), "namespace identity folder");
    UINT sizeColumn = 0;
    for (;; ++sizeColumn) {
        PROPERTYKEY key{};
        Check(sizeColumn < kMaxShellColumns && SUCCEEDED(folder->MapColumnToSCID(sizeColumn, &key)), "Size column");
        if (SameKey(key, PKEY_Size)) break;
    }
    // Rows as a namespace listing produces them: enumerated child IDs.
    std::vector<Entry> rows;
    ComPtr<IEnumIDList> children;
    Check(SUCCEEDED(folder->EnumObjects(nullptr, SHCONTF_NONFOLDERS, &children)) && children, "namespace enumeration");
    for (PITEMID_CHILD child = nullptr; children->Next(1, &child, nullptr) == S_OK;) {
        Entry entry;
        entry.shellChild = SharedPidl(child, TaskMemDeleter{});
        entry.shellIdentity = SharedPidl(ILCombine(parent.get(), child), TaskMemDeleter{});
        entry.path = NamespaceItemKey(entry.shellIdentity.get());
        STRRET name{}; PWSTR text = nullptr;
        Check(SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_INFOLDER | SHGDN_FORPARSING, &name)) &&
            SUCCEEDED(StrRetToStrW(&name, child, &text)) && text, "namespace row name");
        entry.name = text; CoTaskMemFree(text);
        rows.push_back(std::move(entry));
    }
    Check(rows.size() == 2, "namespace identity row count");
    const auto row = [&](const wchar_t* name) -> Entry& {
        const auto it = std::find_if(rows.begin(), rows.end(), [&](const Entry& e) { return _wcsicmp(e.name.c_str(), name) == 0; });
        Check(it != rows.end(), "namespace identity row");
        return *it;
    };
    // The displayed identity of kept.bin, as another Windows path produced it.
    OwnedPidl simple(SHSimpleIDListFromPath((root / L"kept.bin").c_str()));
    Check(simple != nullptr, "simple item ID");
    const PCUITEMID_CHILD simpleChild = ILFindLastID(simple.get());
    Entry& kept = row(L"kept.bin");
    const HRESULT canonical = folder->CompareIDs(SHCIDS_CANONICALONLY, simpleChild, kept.shellChild.get());
    Check(!PidlBytesEqual(simpleChild, kept.shellChild.get()) && SUCCEEDED(canonical) && HRESULT_CODE(canonical) == 0,
        "witness lost: Windows no longer encodes one item with differing IDs");
    NamespaceIdentities previous;
    const std::wstring previousKey = NamespaceItemKey(simple.get());
    previous.emplace(previousKey, SharedPidl(ILClone(simpleChild), TaskMemDeleter{}));
    const std::wstring addedKey = row(L"added.bin").path;
    const SharedPidl enumeratedChild = kept.shellChild; // Carries the item's real size data.
    AdoptPreviousNamespaceIdentities(rows, parent.get(), previous, [] { return true; });
    Check(row(L"kept.bin").path == previousKey && PidlBytesEqual(row(L"kept.bin").shellChild.get(), simpleChild),
        "relisted item lost its displayed identity (reorders animate as remove and insert)");
    Check(row(L"added.bin").path == addedKey, "an item absent from the displayed listing was given another identity");
    // A namespace row that is a real file takes Size from its own folder,
    // even when its displayed ID (the simple one adopted above) has no size.
    SHELLDETAILS details{}; PWSTR expected = nullptr;
    const auto& keptChild = enumeratedChild;
    Check(SUCCEEDED(folder->GetDetailsOf(keptChild.get(), sizeColumn, &details)) &&
        SUCCEEDED(StrRetToStrW(&details.str, keptChild.get(), &expected)) && expected, "folder Size text");
    const std::wstring expectedText = expected; CoTaskMemFree(expected);
    ShellSizeBinding binding; std::wstring text;
    Check(ReadDelegatedSizeDetail(row(L"kept.bin").shellIdentity.get(), text, binding, 1) && text == expectedText,
        "namespace row Size differs from its filesystem folder's Size text");
    // Scope: plain item lists are listed; folders with subfolders stay native.
    PIDLIST_ABSOLUTE apps = nullptr, controlPanel = nullptr;
    Check(SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_AppsFolder, 0, nullptr, &apps)) &&
        SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_ControlPanelFolder, 0, nullptr, &controlPanel)), "read-only scope probe");
    OwnedPidl ownedApps(apps), ownedControlPanel(controlPanel);
    Check(UsesNamespaceFolderIdentity(FolderLocation(apps)), "Applications item list was not supported");
    Check(!IsSupportedLocation(FolderLocation(controlPanel)), "a folder with subfolder pages left the native view");
    binding.Reset(); folder.Reset(); children.Reset();
    Check(root.parent_path() == std::filesystem::path(directory), "namespace identity cleanup boundary");
    Check(ReleasePrivateFixture(root), "namespace identity cleanup");
    std::cout << "PASS: relisted namespace items keep identity, namespace files read their folder's Size, item-list scope\n";
}

// Search results highlight their query words independently of type-to-select,
// and the renderer receives them through the presentation snapshot.
static void TestSearchHighlight(const std::wstring&) {
    using Terms = std::vector<std::wstring>;
    Check(SearchQueryTerms(L"search-ms:displayname=Search%20Results%20in%20Downloads"
        L"&crumb=System.Generic.String%3Alo&crumb=location:C%3A%5CUsers%5Cx%5CDownloads") == Terms{L"lo"},
        "address-bar search query words");
    Check(SearchQueryTerms(L"search-ms:crumb=System.Generic.String%3Akind%3Adocument%20log%20-draft%20"
        L"%22exact%20phrase%22%20OR%20caf%C3%A9") == Terms{L"log", L"exact phrase", L"caf\u00e9"},
        "query filters, exclusions, operators, phrases or UTF-8 escapes");
    Check(SearchQueryTerms(L"C:\\Users\\x\\Downloads").empty(), "non-search folder produced words");
    const auto ranges = [](const std::wstring& name, const Terms& terms) {
        std::vector<std::pair<UINT32, UINT32>> out;
        std::vector<DWRITE_TEXT_RANGE> found;
        SearchHighlightRanges(name, terms, found);
        for (const auto& range : found) out.emplace_back(range.startPosition, range.length);
        return out;
    };
    using Spans = std::vector<std::pair<UINT32, UINT32>>;
    Check(ranges(L"libraries_log", {L"lo"}) == Spans{{10, 2}}, "match inside a name");
    Check(ranges(L"log_lo", {L"lo"}) == Spans{{0, 2}, {4, 2}}, "every occurrence");
    Check(ranges(L"LOG", {L"log"}) == Spans{{0, 3}}, "case-insensitive match");
    Check(ranges(L"Caf\u00e9 menu", {L"cafe"}) == Spans{{0, 4}}, "diacritic-insensitive match");
    Check(ranges(L"report", {L"lo"}).empty(), "absent word highlighted");
    PresentationFixture::Tab tab(L"search highlight", 1, false, false, false, 800, 400);
    auto& state = tab.state; FocusFixture::Session focus(tab.frame, state.hwnd);
    state.searchHighlightTerms = {L"lo"};
    Check(CapturePresentationModel(state)->searchHighlightTerms == Terms{L"lo"},
        "search words never reached the renderer's presentation snapshot");
    std::cout << "PASS: search query words, highlight ranges and their presentation handoff\n";
}
