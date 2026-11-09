// Tiles view: a grid mode in every folder, with name/type/size lines. Real
// files in a private fixture; expectations come from Windows' own type names,
// the folder's own Size column and DPI arithmetic.
static void TestTileLayout(const std::wstring& directory) {
    const auto root = std::filesystem::path(directory) / L"tile-layout";
    std::filesystem::create_directories(root);
    const auto file = root / L"notes.txt";
    {
        HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(handle != INVALID_HANDLE_VALUE, "tile fixture file");
        LARGE_INTEGER length{}; length.QuadPart = 54321;
        const bool sized = SetFilePointerEx(handle, length, nullptr, FILE_BEGIN) && SetEndOfFile(handle);
        CloseHandle(handle);
        Check(sized, "tile fixture length");
    }
    Check(IsGpuViewMode(FVM_TILE), "Tiles must use the GPU grid");
    for (const bool rtl : {false, true}) {
        Regression::Scope scope(rtl ? "tiles rtl" : "tiles ltr");
        PresentationFixture::Tab tab(L"tile layout", 1, rtl, true, false, 900, 500);
        auto& state = tab.state; FocusFixture::Session focus(tab.frame, state.hwnd);
        state.folderPath = root.wstring(); state.loaded = false; state.total = 0; state.entries.clear();
        state.selectedFlags.clear(); state.presentationEntries.reset();
        QueueDirectoryRefresh(state);
        Check(Eventually([&] { ReceiveDirectoryResults(state); return state.loaded && !state.directoryPending; }),
            "tile fixture names");
        Check(state.total == 1, "tile fixture row count");
        ApplyViewLayout(state, {FVM_TILE, 48}, false);
        Check(TileLayout(state) && !DriveTiles(state), "Tiles outside This PC became drive tiles or icons");
        const int dpi = ViewDpi(state);
        const auto item = LayoutItem(state, LayoutRowVisual(state, 0, 1.f));
        Check(std::fabs(item.icon.top - item.text.top) < .01f, "tile icon and name are not top-aligned");
        Check(std::fabs((item.text.left - item.icon.right) - Scale96(12, dpi)) < .01f, "tile icon-to-text gap");
        Check(item.detail[0].top >= item.text.bottom && item.detail[1].top >= item.detail[0].bottom &&
            item.detail[1].bottom <= item.cell.bottom + .01f && item.icon.bottom <= item.cell.bottom + .01f,
            "tile lines overlap or leave their cell");
        Check(state.rowPx >= item.icon.bottom - item.icon.top + Scale96(12, dpi) - .01f, "tile row clips its icon");
        Check(std::fabs(item.icon.bottom - item.detail[1].bottom) <= 1.f,
            "tile icon does not span its reserved text lines");
        const float pitch = item.detail[0].top - item.text.top;
        Check(pitch == std::round(pitch) && item.detail[1].top - item.detail[0].top == pitch,
            "tile lines are not evenly spaced on whole pixels");
        // Tiles request exactly their lines; values must equal Windows' own.
        QueueVisibleDetails(state);
        SHFILEINFOW info{};
        Check(SHGetFileInfoW(file.c_str(), 0, &info, sizeof(info), SHGFI_TYPENAME) != 0, "Windows type name");
        ComPtr<IShellItem> folderItem; ComPtr<IShellFolder2> folder;
        Check(SUCCEEDED(SHCreateItemFromParsingName(root.c_str(), nullptr, IID_PPV_ARGS(&folderItem))) &&
            SUCCEEDED(folderItem->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&folder))), "tile fixture folder");
        UINT sizeColumn = 0;
        for (;; ++sizeColumn) {
            PROPERTYKEY key{};
            Check(sizeColumn < kMaxShellColumns && SUCCEEDED(folder->MapColumnToSCID(sizeColumn, &key)), "Size column");
            if (SameKey(key, PKEY_Size)) break;
        }
        PIDLIST_RELATIVE child = nullptr; auto leaf = file.filename().wstring();
        Check(SUCCEEDED(folder->ParseDisplayName(nullptr, nullptr, leaf.data(), nullptr, &child, nullptr)), "tile child");
        OwnedPidl ownedChild(child);
        SHELLDETAILS details{}; PWSTR sizeText = nullptr;
        Check(SUCCEEDED(folder->GetDetailsOf(child, sizeColumn, &details)) &&
            SUCCEEDED(StrRetToStrW(&details.str, child, &sizeText)) && sizeText, "folder Size text");
        const std::wstring expectedSize = sizeText; CoTaskMemFree(sizeText);
        Check(Eventually([&] {
            ReceiveDirectoryResults(state);
            const auto& entry = state.entries.front();
            return entry.type == info.szTypeName && entry.size == expectedSize;
        }) || [&] {
            std::wcout << L"TILE type=[" << state.entries.front().type << L"] expected=[" << info.szTypeName
                << L"] size=[" << state.entries.front().size << L"] expected=[" << expectedSize << L"]\n";
            return false;
        }(), "tile type/size lines differ from Windows");
        // Details after Tiles must request its own columns, not reuse completion.
        const auto epoch = state.columnEpoch;
        ApplyViewLayout(state, {FVM_DETAILS, 48}, false);
        Check(state.columnEpoch != epoch, "leaving Tiles kept tile-complete rows for Details columns");
        folder.Reset(); folderItem.Reset(); // The fixture stops this tab's workers.
    }
    Check(root.parent_path() == std::filesystem::path(directory), "tile fixture cleanup boundary");
    Check(ReleasePrivateFixture(root), "tile fixture cleanup");
    std::cout << "PASS: Tiles in ordinary folders: top-aligned icon/name, DPI gap, reserved lines, Windows type/size, Details handoff (LTR/RTL)\n";
}

// Content view: a list mode with name/type and date/size lines. Values come
// from the folder's own columns; switches into and out of List and Details
// start from the presented layout and settle on the destination.
static void TestContentLayout(const std::wstring& directory) {
    const auto root = std::filesystem::path(directory) / L"content-layout";
    std::filesystem::create_directories(root);
    const auto file = root / L"notes.txt";
    std::ofstream(file) << std::string(54321, 'x');
    Check(IsGpuViewMode(FVM_CONTENT) && !IsGpuViewMode(FVM_THUMBSTRIP), "Content must use the GPU list");
    ComPtr<IShellItem> folderItem; ComPtr<IShellFolder2> folder;
    Check(SUCCEEDED(SHCreateItemFromParsingName(root.c_str(), nullptr, IID_PPV_ARGS(&folderItem))) &&
        SUCCEEDED(folderItem->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&folder))), "content fixture folder");
    PIDLIST_RELATIVE child = nullptr; auto leaf = file.filename().wstring();
    Check(SUCCEEDED(folder->ParseDisplayName(nullptr, nullptr, leaf.data(), nullptr, &child, nullptr)), "content child");
    OwnedPidl ownedChild(child);
    // The folder's own column text for a property, as its Details view shows it.
    const auto columnText = [&](REFPROPERTYKEY wanted) {
        for (UINT column = 0; column < kMaxShellColumns; ++column) {
            PROPERTYKEY key{};
            Check(SUCCEEDED(folder->MapColumnToSCID(column, &key)), "content fixture column");
            if (!SameKey(key, wanted)) continue;
            SHELLDETAILS details{}; PWSTR text = nullptr;
            Check(SUCCEEDED(folder->GetDetailsOf(child, column, &details)) &&
                SUCCEEDED(StrRetToStrW(&details.str, child, &text)) && text, "content column text");
            std::wstring value = text; CoTaskMemFree(text);
            return value;
        }
        return std::wstring();
    };
    const std::wstring type = columnText(PKEY_ItemTypeText), date = columnText(PKEY_DateModified),
        size = columnText(PKEY_Size);
    Check(!type.empty() && !date.empty() && !size.empty(), "folder columns for the Content lines");
    folder.Reset(); folderItem.Reset(); // Expectations are read; hold no fixture handle.
    for (const bool rtl : {false, true}) {
        Regression::Scope scope(rtl ? "content rtl" : "content ltr");
        PresentationFixture::Tab tab(L"content layout", 1, rtl, false, false, 900, 500);
        auto& state = tab.state; FocusFixture::Session focus(tab.frame, state.hwnd);
        state.folderPath = root.wstring(); state.loaded = false; state.total = 0; state.entries.clear();
        state.selectedFlags.clear(); state.presentationEntries.reset();
        QueueDirectoryRefresh(state);
        Check(Eventually([&] { ReceiveDirectoryResults(state); return state.loaded && !state.directoryPending; }),
            "content fixture names");
        Check(state.total == 1, "content fixture row count");
        ApplyViewLayout(state, {FVM_LIST, 16}, false);
        const auto list = LayoutItem(state, LayoutRowVisual(state, 0, 1.f));
        const float listRow = state.rowPx;
        // Entering Content zooms from the presented list row: no jump.
        ApplyViewLayout(state, {FVM_CONTENT, 32}, true, true);
        Check(state.zoomAnimating && !state.gridView && !state.columnsView, "List to Content did not animate");
        const auto start = LayoutItem(state, LayoutRowVisual(state, 0, 1.f));
        Check(std::fabs(state.rowPx - listRow) < .5f && std::fabs(start.text.top - list.text.top) < .5f &&
            std::fabs(start.icon.top - list.icon.top) < .5f && ContentReveal(state) < .05f,
            "List to Content jumped instead of starting from the list layout");
        AdvanceRowHeight(state, true);
        Check(!state.zoomAnimating && ContentReveal(state) == 1.f && state.rowPx > listRow,
            "List to Content did not settle on taller Content rows");
        const int dpi = ViewDpi(state);
        const auto item = LayoutItem(state, LayoutRowVisual(state, 0, 1.f));
        const float rowTop = LayoutRowVisual(state, 0, 1.f).top;
        Check(std::fabs(item.icon.top - item.text.top) < .01f, "content icon and name are not top-aligned");
        Check(item.detail[0].top >= item.text.bottom && item.aside[1].top >= item.aside[0].bottom &&
            item.aside[0].left >= item.text.right + Scale96(16, dpi) - .01f &&
            std::fabs(item.aside[0].top - item.text.top) < .01f &&
            std::fabs(item.aside[1].top - item.detail[0].top) < .01f,
            "content lines overlap or are not paired in two columns");
        Check(item.icon.top >= rowTop - .01f && item.detail[0].bottom <= rowTop + state.rowPx + .01f &&
            item.icon.bottom <= rowTop + state.rowPx + .01f, "content lines leave their row");
        // Content requests exactly its lines; values must equal the folder's own.
        QueueVisibleDetails(state);
        Check(Eventually([&] {
            ReceiveDirectoryResults(state);
            const auto& entry = state.entries.front();
            return entry.type == type && entry.date == date && entry.size == size;
        }) || [&] {
            const auto& entry = state.entries.front();
            std::wcout << L"CONTENT type=[" << entry.type << L"/" << type << L"] date=[" << entry.date << L"/"
                << date << L"] size=[" << entry.size << L"/" << size << L"]\n";
            return false;
        }(), "content lines differ from the folder's columns");
        // Leaving for Details zooms back from Content and requests columns.
        const float contentRow = state.rowPx;
        const auto epoch = state.columnEpoch;
        ApplyViewLayout(state, {FVM_DETAILS, 16}, true, true);
        Check(state.zoomAnimating && state.columnsView && std::fabs(state.rowPx - contentRow) < .5f &&
            ContentReveal(state) > .95f, "Content to Details did not start from the Content layout");
        Check(state.columnEpoch != epoch, "leaving Content kept content-complete rows for Details columns");
        AdvanceRowHeight(state, true); FinishColumnTransition(state);
        Check(!state.zoomAnimating && ContentReveal(state) == 0.f && state.rowPx < contentRow,
            "Content to Details did not settle on list rows");
    }
    Check(root.parent_path() == std::filesystem::path(directory), "content fixture cleanup boundary");
    Check(ReleasePrivateFixture(root), "content fixture cleanup");
    std::cout << "PASS: Content in ordinary folders: top-aligned name/type and date/size columns, folder's own values, "
        "List/Details zoom in and out from the presented layout (LTR/RTL)\n";
}
