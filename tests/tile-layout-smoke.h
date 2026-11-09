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
    Check(IsGpuViewMode(FVM_TILE) && !IsGpuViewMode(FVM_CONTENT), "Tiles must use the GPU grid; Content stays native");
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
