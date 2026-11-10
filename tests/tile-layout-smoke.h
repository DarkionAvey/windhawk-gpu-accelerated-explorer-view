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
        const float expectedPitch=state.fontPx*1.5f+float(MulDiv(2,dpi,96));
        Check(std::fabs(pitch-expectedPitch)<.01f &&
            std::fabs(item.detail[1].top-item.detail[0].top-expectedPitch)<.01f,
            "tile lines lost their evenly spaced fractional layout pitch");
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
        // Details to Medium icons morphs each item: its icon and name start
        // where the list drew them and end in the grid cell.
        const auto row = LayoutItem(state, LayoutRowVisual(state, 0, 1.f));
        ApplyViewLayout(state, {FVM_ICON, 48}, true, true);
        FinishColumnTransition(state);
        Check(state.gridView && state.rowTransitionActive && state.reflowLayoutTransition,
            "Details to Medium icons did not start a layout morph");
        const auto first = LayoutItem(state, LayoutRowVisual(state, 0, 0.f));
        const auto same = [](D2D1_RECT_F a, D2D1_RECT_F b) {
            return std::fabs(a.left - b.left) < .5f && std::fabs(a.top - b.top) < .5f &&
                std::fabs(a.right - b.right) < .5f && std::fabs(a.bottom - b.bottom) < .5f;
        };
        Check(same(first.icon, row.icon) && same(first.text, row.text),
            "Details to Medium icons replaced the list item instead of moving its icon and name");
        StopRowTransition(state);
        const auto cell = LayoutItem(state, LayoutRowVisual(state, 0, 1.f));
        Check(cell.icon.right - cell.icon.left > row.icon.right - row.icon.left &&
            cell.text.top >= cell.icon.bottom - .01f, "the morph did not settle on a grid cell");
    }
    Check(root.parent_path() == std::filesystem::path(directory), "content fixture cleanup boundary");
    Check(ReleasePrivateFixture(root), "content fixture cleanup");
    std::cout << "PASS: Content in ordinary folders: top-aligned name/type and date/size columns, folder's own values, "
        "List/Details zoom in and out from the presented layout (LTR/RTL)\n";
}

// Explorer's Compact view: list rows take Explorer's measured compact
// fraction of the configured row (DPI arithmetic), keep their text size and
// zoom there from the presented row; grids are unaffected.
static void TestCompactRows(const std::wstring&) {
    {
        Regression::Scope scope("compact rows");
        PresentationFixture::Tab tab(L"compact rows", 40, false, false, false, 900, 600);
        auto& state = tab.state; state.compactRows = false; const int dpi = ViewDpi(state);
        ApplyViewLayout(state, {FVM_DETAILS, 16}, false); UpdateMetrics(state);
        const float row = state.rowPx, font = state.fontPx;
        Check(row == float(MulDiv(g_row96.load(), dpi, 96)), "fixture row is not the DPI-scaled setting");
        struct Reset { ~Reset() { ExplorerSettingsFixture::compact = false; } } reset;
        ExplorerSettingsFixture::compact = true;
        ReconcileCompactRows(state);
        Check(state.zoomAnimating && std::fabs(state.rowPx - row) < .01f, "Compact view did not zoom from the presented row");
        AdvanceRowHeight(state, true);
        Check(state.rowPx == std::round(row * 27.f / 37.f), "Compact rows differ from Explorer's measured 27/37 fraction");
        Check(state.fontPx == font, "Compact view changed the text size");
        // Compact highlights touch; a check box stays inside its highlight
        // with the same margin proportion it has in a relaxed row.
        const float top = LayoutRowVisual(state, 0, 1.f).top;
        const auto band = RowHighlightRect(state, top, state.rowPx);
        Check(band.top == top && band.bottom == top + state.rowPx, "Compact rows kept a gap between highlights");
        state.clickPolicy.checkSelect = true; state.checkboxRevealStarted = {};
        const auto box = LayoutItem(state, LayoutRowVisual(state, 0, 1.f)).checkbox;
        const float side = box.bottom - box.top;
        Check(box.top >= band.top && box.bottom <= band.bottom &&
            std::fabs(side / (band.bottom - band.top) - 20.f / 28.f) < .01f,
            "the check box overflows or loses its proportion in a compact row");
        state.clickPolicy.checkSelect = false;
        ApplyViewLayout(state, {FVM_ICON, 48}, false);
        const float compactGrid = state.rowPx;
        ExplorerSettingsFixture::compact = false; ReconcileCompactRows(state);
        Check(state.rowPx == compactGrid, "Compact view changed grid cells");
    }
    std::cout << "PASS: Compact view shrinks list rows by Explorer's fraction with unchanged text, animated, grids unchanged\n";
}

// Reversing the sorted column folds its chevron through flat on the short
// hover clock; a new column shows its chevron at rest, and a reversal during
// a flip continues from the presented pose.
static void TestSortChevronFlip(const std::wstring&) {
    PresentationFixture::Tab tab(L"sort chevron", 5);
    auto& state = tab.state;
    // Each paint samples the clock once, then tracks the sort; do the same.
    const auto paint = [&] { QueryPerformanceCounter(&state.itemOpacityNow); TrackSortChevron(state); };
    state.sort.explicitOrder = true; state.sort.key = PKEY_ItemNameDisplay; state.sort.direction = SORT_ASCENDING;
    paint();
    Check(!SortChevronFlipping(state) && SortChevronFlip(state) == 1.f, "the first sort animated its chevron");
    state.sort.direction = SORT_DESCENDING; paint();
    Check(SortChevronFlipping(state) && SortChevronFlip(state) < -.9f, "reversing the column did not start from the old direction");
    state.sort.direction = SORT_ASCENDING; paint();
    Check(SortChevronFlip(state) > .9f, "a reversal during the flip jumped instead of continuing from the presented pose");
    state.sort.direction = SORT_DESCENDING; paint();
    LARGE_INTEGER started = state.sortChevronFlipStarted;
    Check(Eventually([&] { return !SortChevronFlipping(state); }) &&
        (paint(), SortChevronFlip(state) == 1.f),
        "the chevron flip did not settle");
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    Check((now.QuadPart - started.QuadPart) * 1000. / state.qpcFrequency >= double(kHoverAnimationDurationMs) - 1.,
        "the chevron flip ended before the hover duration");
    state.sort.key = PKEY_Size; paint();
    Check(!SortChevronFlipping(state) && SortChevronFlip(state) == 1.f, "sorting a new column animated its chevron");
    std::cout << "PASS: sort chevron flips through flat on reversal only, continues mid-flip and settles on the hover clock\n";
}

// Thumbnail shadows follow the image's outline: edge-to-edge images (photos,
// filmstrip frames) are marked for a rectangular shadow; shaped previews with
// transparent margins (fonts, documents) are not.
static void TestThumbnailShadowShape(const std::wstring&) {
    const auto image = [](UINT width, UINT height, UINT margin) {
        IconPixels pixels; pixels.width = width; pixels.height = height; pixels.stride = width * 4;
        pixels.data.resize(size_t(pixels.stride) * height);
        for (UINT y = margin; y + margin < height; ++y)
            for (UINT x = margin; x + margin < width; ++x) {
                auto* p = pixels.data.data() + size_t(y) * pixels.stride + size_t(x) * 4;
                p[0] = p[1] = p[2] = 128; p[3] = 255;
            }
        return pixels;
    };
    Check(DecorateThumbnail(image(96, 54, 0), ThumbnailStyle{}).thumbnailOpaque,
        "an edge-to-edge image lost its rectangular shadow");
    Check(!DecorateThumbnail(image(96, 96, 12), ThumbnailStyle{}).thumbnailOpaque,
        "a shaped preview with transparent margins kept a rectangular shadow");
    // Film rails follow Explorer's own view, measured at 96 and 120 DPI: the
    // rail is 0.121 of the frame's long side, holes 0.056 of it tall every
    // 0.109 of it, so frames of one slot share a hole size and taller frames
    // show more holes. The end slots stay closed (no hole meets a rounded
    // corner). Rails overlay the image's edges: the composite keeps the
    // image's size and shadow, and the holes show the image beneath.
    ThumbnailStyle film; film.filmstrip = true;
    std::vector<int> holeHeights;
    for (const UINT height : {140u, 240u}) {
        auto source = image(320, height, 0);
        for (size_t i = 0; i < source.data.size(); i += 4) { source.data[i] = 220; source.data[i + 1] = 40; source.data[i + 2] = 30; }
        const auto frame = DecorateThumbnail(std::move(source), film);
        const UINT rail = UINT(std::lround(320 * .121));
        Check(frame.thumbnailOpaque && frame.thumbnailWidth == 320 && frame.thumbnailHeight == height,
            "film rails changed the image's size or removed its shadow");
        const auto at = [&](UINT x, UINT y) { return frame.data.data() + size_t(y) * frame.stride + size_t(x) * 4; };
        const auto shows = [&](UINT x, UINT y) { const auto* p = at(x, y); return p[0] == 220 && p[1] == 40 && p[2] == 30 && p[3] == 255; };
        // Explorer's count (the last hole needs only its own height) less one for the corners.
        const int expected = int(std::floor((height - 320 * .056) / (320 * .109)));
        const UINT hole = rail / 2; // Holes are centered across the rail.
        for (const UINT column : {hole, 319 - hole}) {
            int holes = 0, run = 0, tallest = 0; bool open = false;
            for (UINT y = 0; y < height; ++y) {
                const bool clear = shows(column, y);
                if (clear && !open) { ++holes; run = 0; }
                if (clear) tallest = std::max(tallest, ++run);
                open = clear;
            }
            Check(holes == expected, "a film rail does not repeat its holes to fit the frame minus the corner slots");
            Check(!shows(column, 0) && !shows(column, height - 1), "a film hole meets the frame's top or bottom corner");
            holeHeights.push_back(tallest);
        }
        // Across a hole's middle row, the rail on either side is equally wide.
        UINT row = 0; while (row < height && !shows(rail / 2, row)) ++row;
        int holeLeft = -1, holeRight = -1;
        for (UINT x = 0; x < rail; ++x) if (shows(x, row + 2)) { if (holeLeft < 0) holeLeft = int(x); holeRight = int(x); }
        Check(holeLeft >= 0 && std::abs(holeLeft - (int(rail) - 1 - holeRight)) <= 1, "film holes are not centered across the rail");
        const auto* bar = at(1, 4);
        Check(bar[0] == bar[1] && bar[1] == bar[2] && bar[0] == 28 && bar[3] == 255 && shows(rail + 1, height / 2),
            "film rail is not its near-black grey, or covered the image beyond it");
    }
    Check(std::abs(holeHeights.front() - holeHeights.back()) <= 1, "film holes changed size with the frame's height");
    std::cout << "PASS: thumbnail shadows frame edge-to-edge images; film rails overlay them with slot-sized holes repeated to fit\n";
}
