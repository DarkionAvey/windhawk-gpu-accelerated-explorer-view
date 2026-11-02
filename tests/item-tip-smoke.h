#include <thread>
// Hovered-item tooltip: real popup windows, DirectWrite layout, DWM attributes
// and the tab's Columns lane, on a private desktop that is never displayed.
// Hover is the production entry point after the system hover time; no input
// is injected, and the user's focus, cursor and desktop are untouched.
static void ItemTipScenario(const std::wstring& directory) {
    Check(g_detailedItemTips.load(), "detailed tooltips must default to on");
    g_detailedItemTips = false;
    const auto root = std::filesystem::path(directory) / L"item-tips";
    std::filesystem::create_directories(root / L"folder");
    const std::wstring longName = L"quarterly-report-" + std::wstring(100, L'x') + L".txt";
    for (const auto& name : {std::wstring(L"note.txt"), longName}) {
        HANDLE file = CreateFileW((root / name).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "item tip fixture file");
        DWORD written = 0;
        const bool wrote = WriteFile(file, "item tip", 8, &written, nullptr) && written == 8;
        CloseHandle(file);
        Check(wrote, "item tip fixture contents");
    }
    {
        // Real JPEG property handler, including writable comments and tags.
        ComPtr<IWICImagingFactory> wic; ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame;
        Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
            SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename((root / L"photo.jpg").c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder)) &&
            SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)), "tooltip JPEG encoder");
        auto format = GUID_WICPixelFormat24bppBGR;
        std::vector<BYTE> pixels(32 * 24 * 3, 128);
        Check(SUCCEEDED(frame->SetSize(32, 24)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
            SUCCEEDED(frame->WritePixels(24, 32 * 3, UINT(pixels.size()), pixels.data())) &&
            SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()), "tooltip JPEG contents");
    }
    {
        ComPtr<IPropertyStore> store;
        Check(SUCCEEDED(EntryPropertyStore((root / L"photo.jpg").wstring(), nullptr, GPS_READWRITE, &store)), "JPEG metadata writer");
        PROPVARIANT comment{}, tags{};
        const std::wstring text = L"Private fixture comment\r\n" + std::wstring(2048, L'x');
        const wchar_t* keywords[]{L"fixture", L"tooltip"};
        Check(SUCCEEDED(InitPropVariantFromString(text.c_str(), &comment)) &&
            SUCCEEDED(InitPropVariantFromStringVector(keywords, ARRAYSIZE(keywords), &tags)) &&
            SUCCEEDED(store->SetValue(PKEY_Comment, comment)) && SUCCEEDED(store->SetValue(PKEY_Keywords, tags)) &&
            SUCCEEDED(store->Commit()), "JPEG comments/tags");
        PropVariantClear(&comment); PropVariantClear(&tags);
    }
    {
        // One second of PCM audio; Windows resolves bitrate from the WAV.
        std::vector<BYTE> wave(44 + 16000);
        memcpy(wave.data(), "RIFF", 4); memcpy(wave.data() + 8, "WAVEfmt ", 8); memcpy(wave.data() + 36, "data", 4);
        const auto u32 = [&](size_t at, DWORD value) { memcpy(wave.data() + at, &value, sizeof(value)); };
        const auto u16 = [&](size_t at, WORD value) { memcpy(wave.data() + at, &value, sizeof(value)); };
        u32(4, DWORD(wave.size() - 8)); u32(16, 16); u16(20, 1); u16(22, 1);
        u32(24, 8000); u32(28, 16000); u16(32, 2); u16(34, 16); u32(40, 16000);
        HANDLE file = CreateFileW((root / L"audio.wav").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
        DWORD written = 0;
        Check(file != INVALID_HANDLE_VALUE && WriteFile(file, wave.data(), DWORD(wave.size()), &written, nullptr) &&
            written == wave.size(), "tooltip PCM fixture");
        CloseHandle(file);
    }
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))), "item tip fonts");
    Check(RegisterItemTipClass(OverlayModuleInstance()), "item tip window class");
    // An on-monitor view, so placement below the cursor is not clamped.
    const HWND view = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"private item tip view",
        WS_POPUP | WS_VISIBLE, 700, 200, 600, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(view != nullptr, "item tip view");
    {
    ViewState state;
    state.hwnd = state.frameHwnd = state.activeTabHost = state.background->hwnd = view;
    state.folderPath = root.wstring();
    state.rowPx = 34;
    QueueDirectoryRefresh(state);
    Check(Eventually([&] {
        ReceiveDirectoryResults(state);
        return state.loaded && !state.directoryPending;
    }), "item tip names completion");
    Check(state.total == 5, "item tip fixture row count");
    SetWindowLongPtrW(view, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    Check(MakeTextFormats(state) && state.headerFormat, "item tip text formats");
    state.visible = state.inScope = state.pointerInside = true;
    state.pointingDevice = PointingDevice::Mouse;
    state.clickPolicy.itemTips = true;
    state.nativeAppBackground = RGB(25, 25, 25); state.nativeAppForeground = RGB(255, 255, 255);
    state.nativeTextColor = RGB(255, 255, 255);
    RefreshItemTipPalette(state, false);
    const auto rowOf = [&](const std::wstring& name) {
        for (int row = 0; row < state.total; ++row)
            if (state.entries[size_t(row)].name == name) return row;
        Check(false, "item tip fixture row");
        return -1;
    };
    const int note = rowOf(L"note.txt"), folder = rowOf(L"folder"), wide = rowOf(longName);
    auto& tip = state.itemTip;
    const POINT client{40, 60};
    const auto shown = [&] { return IsWindow(tip.hwnd) && IsWindowVisible(tip.hwnd); };
    // Production hover entry; lane results arrive through the owner's queue.
    const auto hover = [&](int row) {
        ItemTipHoverMoved(state, row);
        state.hovered = row;
        ShowItemTip(state, client);
        if (tip.token) Check(!shown(), "partial tooltip appeared before its metadata was ready");
        return Eventually([&] {
            MSG message{};
            while (PeekMessageW(&message, view, kMsgItemTipReady, kMsgItemTipReady, PM_REMOVE)) ConsumeItemTip(state);
            return shown() && !tip.token;
        });
    };
    const auto valueOf = [&](REFPROPERTYKEY key) -> std::wstring {
        for (const auto& row : tip.rows) if (SameKey(row.key, key)) return row.value;
        return {};
    };
    SHFILEINFOW textType{}, folderType{};
    Check(SHGetFileInfoW(L"note.txt", FILE_ATTRIBUTE_NORMAL, &textType, sizeof(textType),
            SHGFI_USEFILEATTRIBUTES | SHGFI_TYPENAME) &&
        SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &folderType, sizeof(folderType),
            SHGFI_USEFILEATTRIBUTES | SHGFI_TYPENAME), "Shell type names");

    // A file: full name, then Size, Type, Created, Modified; Type text came
    // from the lane because this view has no Type column.
    Check(hover(note), "file item tip did not appear");
    Check(state.directoryToken && state.background->itemTipListing.load() == state.directoryToken,
        "item tip Size binding is not scoped to the listing");
    const Entry& file = state.entries[size_t(note)];
    Check(tip.name == file.name && tip.rows.size() >= 3, "file item tip rows");
    ShellSizeBinding sizeBinding; std::wstring shellSize;
    Check(ReadShellSizeDetail(file.path, nullptr, shellSize, sizeBinding, 1), "native tooltip size");
    Check(valueOf(PKEY_Size) == shellSize && valueOf(PKEY_ItemTypeText) == textType.szTypeName,
        "file item tip size/type differ from Shell");
    Check(valueOf(PKEY_DateCreated).starts_with(FormatFileTime(file.created)) &&
        (FormatFileTime(file.created) == FormatFileTime(file.lastWrite) ? valueOf(PKEY_DateModified).empty() :
            valueOf(PKEY_DateModified).starts_with(FormatFileTime(file.lastWrite))), "file item tip dates/deduplication");
    for (const auto& row : tip.rows) {
        ComPtr<IPropertyDescription> description; PWSTR label = nullptr;
        Check(SUCCEEDED(PSGetPropertyDescription(row.key, IID_PPV_ARGS(&description))) &&
            SUCCEEDED(description->GetDisplayName(&label)) && label && row.label == label &&
            !row.value.empty(), "tooltip labels must come from Windows, values must be nonempty");
        CoTaskMemFree(label);
    }
    Check(tip.rows.size() <= 4, "standard tooltip unexpectedly included extra properties");
    g_detailedItemTips = true;
    const auto checkProperty = [&](REFPROPERTYKEY key, const std::filesystem::path& path) {
        ComPtr<IPropertyStore> store;
        Check(SUCCEEDED(EntryPropertyStore(path.wstring(), nullptr, GPS_DEFAULT, &store)), "tooltip fixture property store");
        auto expected = FormatShellDetail(store.Get(), key);
        for (auto& ch : expected) if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
        Check(!expected.empty() && valueOf(key) == expected, "tooltip omitted or reformatted populated file metadata");
    };
    Check(hover(rowOf(L"photo.jpg")), "JPEG tooltip");
    for (const auto& key : {PKEY_Image_Dimensions, PKEY_Comment, PKEY_Keywords}) checkProperty(key, root / L"photo.jpg");
    Check(valueOf(PKEY_Capacity).empty() && valueOf(PKEY_FreeSpace).empty() && valueOf(PKEY_FileOwner).empty(),
        "image tooltip included disk or inherited filesystem context");
    std::unordered_set<std::wstring> uniqueValues;
    for (const auto& row : tip.rows)
        Check(uniqueValues.insert(ItemTipComparison(row.value, row.date)).second, "tooltip worker retained duplicate values");
    Check(hover(rowOf(L"audio.wav")), "audio tooltip");
    checkProperty(PKEY_Audio_EncodingBitrate, root / L"audio.wav");
    Check(hover(note), "return to file tooltip");
    // Tab-stop columns: every value starts on one edge after the widest label.
    float widestLabel = 0.f;
    for (const auto& [label, value] : tip.rowLayouts) {
        DWRITE_TEXT_METRICS metrics{};
        label->GetMetrics(&metrics);
        widestLabel = std::max(widestLabel, metrics.widthIncludingTrailingWhitespace);
    }
    Check(tip.labelX == tip.padX && tip.valueX >= tip.labelX + widestLabel, "item tip columns overlap");
    RECT window{}, viewRect{};
    GetWindowRect(tip.hwnd, &window); GetWindowRect(view, &viewRect);
    Check(window.right - window.left == tip.size.cx && window.bottom - window.top == tip.size.cy &&
        window.left == viewRect.left + client.x && window.top > viewRect.top + client.y, "item tip is not below the cursor");
    const LONG_PTR ex = GetWindowLongPtrW(tip.hwnd, GWL_EXSTYLE);
    Check((ex & WS_EX_NOACTIVATE) && (ex & WS_EX_TOOLWINDOW) &&
        GetWindow(tip.hwnd, GW_OWNER) == view, "item tip window is activatable or unowned");
    DWM_WINDOW_CORNER_PREFERENCE corners{};
    if (SUCCEEDED(DwmGetWindowAttribute(tip.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners))))
        Check(corners == DWMWCP_ROUNDSMALL, "item tip does not use the system tooltip corners");
    Check(SendMessageW(tip.hwnd, WM_NCHITTEST, 0, MAKELPARAM(window.left + 1, window.top + 1)) == HTTRANSPARENT,
        "item tip intercepts the pointer");
    UpdateWindow(tip.hwnd);
    Check(tip.target != nullptr, "item tip did not paint through Direct2D");

    // A provider may report a folder size, including a size supplied by another
    // mod. Do not assume folders must be blank. Cached empty cells are checked below.
    Check(hover(folder), "folder tooltip completion");
    Check(valueOf(PKEY_ItemTypeText) == folderType.szTypeName,
        "folder item tip rows");
    // A long name wraps instead of widening the tip past a third of the monitor.
    Check(hover(wide), "long-name item tip did not appear");
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(view, MONITOR_DEFAULTTONEAREST), &monitor);
    // The rule depends on this monitor: compare the name's single-line width
    // with the limit (the details' width, or a third of the working area).
    DWRITE_TEXT_METRICS nameMetrics{}, singleLine{}, valueMetrics{};
    tip.nameLayout->GetMetrics(&nameMetrics);
    ComPtr<IDWriteTextLayout> unwrapped;
    Check(SUCCEEDED(g_writeFactory->CreateTextLayout(tip.name.c_str(), UINT32(tip.name.size()),
        tip.nameFormat.Get(), 1e6f, 1e6f, &unwrapped)) && SUCCEEDED(unwrapped->GetMetrics(&singleLine)),
        "long item name measurement");
    float table = 0.f;
    for (const auto& [label, value] : tip.rowLayouts) {
        value->SetMaxWidth(1e6f); value->GetMetrics(&valueMetrics);
        table = std::max(table, tip.valueX - tip.labelX + valueMetrics.widthIncludingTrailingWhitespace);
    }
    const float limit = std::max(std::ceil(table), float(monitor.rcWork.right - monitor.rcWork.left) / 3.f);
    Check(singleLine.lineCount == 1 && (singleLine.widthIncludingTrailingWhitespace > limit + 1.f ?
        nameMetrics.lineCount > 1 && tip.size.cx <= LONG(std::ceil(limit + 2.f * tip.padX)) + 1 :
        nameMetrics.lineCount == 1), "long item name did not wrap at its limit");

    // Right-to-left Explorer mirrors the columns and the anchor edge.
    state.rtlLayout = true;
    ItemTipHoverMoved(state, -1);
    Check(hover(note) && tip.valueX < tip.labelX, "RTL item tip columns are not mirrored");
    GetWindowRect(tip.hwnd, &window);
    Check(window.right == viewRect.left + client.x, "RTL item tip is not anchored at the cursor's right");
    state.rtlLayout = false;

    // Moving to another item hides it; input dismisses an item until the
    // pointer reaches another item; a stale lane result never shows.
    ItemTipHoverMoved(state, folder);
    Check(!shown() && tip.item.empty(), "item tip survived a hover change");
    state.hovered = -1; ItemTipHoverMoved(state, -1);
    Check(hover(note), "item tip did not reappear");
    HideItemTip(state, true);
    state.hovered = note; ShowItemTip(state, client);
    Check(!shown() && tip.item.empty() && tip.suppressed == state.entries[size_t(note)].path,
        "dismissed item tip reappeared on the same item");
    ItemTipHoverMoved(state, folder);
    Check(tip.suppressed.empty() && hover(folder), "dismissal outlived the item");
    ItemTipHoverMoved(state, -1);
    state.hovered = note; ShowItemTip(state, client);
    Check(tip.token != 0, "item tip lane request");
    HideItemTip(state);
    Sleep(50);
    MSG message{};
    while (PeekMessageW(&message, view, kMsgItemTipReady, kMsgItemTipReady, PM_REMOVE)) ConsumeItemTip(state);
    Check(!shown(), "a cancelled item tip appeared");
    // Hidden views, presses and Folder Options' setting never show a tip.
    for (int gate = 0; gate < 3; ++gate) {
        state.visible = gate != 0; state.mousePressActive = gate == 1; state.clickPolicy.itemTips = gate != 2;
        state.hovered = note; ShowItemTip(state, client);
        Check(!shown() && !tip.token && tip.item.empty(), "ineligible hover requested an item tip");
        ItemTipHoverMoved(state, -1);
    }
    state.visible = true; state.mousePressActive = false; state.clickPolicy.itemTips = true;

    // Theme: a raised surface on dark and light pages, muted labels between
    // background and text, and the exact system colors in high contrast.
    for (const auto& [page, text] : {std::pair{RGB(25, 25, 25), RGB(255, 255, 255)},
                                     std::pair{RGB(255, 255, 255), RGB(0, 0, 0)}}) {
        state.nativeAppBackground = page; state.nativeAppForeground = state.nativeTextColor = text;
        RefreshItemTipPalette(state, false);
        Check(tip.background == RaisedSurface(page, text) && tip.background != page && tip.foreground == text &&
            tip.label != text && tip.label != tip.background, "item tip theme colors");
    }
    RefreshItemTipPalette(state, true);
    Check(tip.background == GetSysColor(COLOR_INFOBK) && tip.foreground == GetSysColor(COLOR_INFOTEXT),
        "item tip ignores high contrast colors");

    // The opaque popup uses exactly the same system/subpixel or explicit
    // smooth/aliased policy as the view, including changes while it is open.
    Check(hover(note), "font policy tooltip");
    const auto oldAntialiasing = g_textAntialiasing.load();
    for (auto mode : {TextAntialiasing::System, TextAntialiasing::Smooth, TextAntialiasing::Aliased}) {
        g_textAntialiasing = mode; ++g_textRenderingRevision;
        InvalidateRect(tip.hwnd, nullptr, FALSE); UpdateWindow(tip.hwnd);
        Check(tip.textStyle.textAntialiasing == mode && tip.textStyle.textRenderingParams,
            "tooltip ignored the selected text rendering policy");
        ComPtr<IDWriteRenderingParams> effective; tip.target->GetTextRenderingParams(&effective);
        // A native font customizer (MacType) rewrites the target's policy, as
        // the list and drag-preview suites already recognize.
        if (tip.target->GetTextAntialiasMode() != tip.textStyle.opaqueTextAntialiasMode ||
            effective.Get() != tip.textStyle.textRenderingParams.Get())
            Check(GetModuleHandleW(L"MacType64.dll") || GetModuleHandleW(L"MacType64.Core.dll"),
                "unexplained tooltip text policy override");
    }
    g_textAntialiasing = oldAntialiasing; ++g_textRenderingRevision;

    // Reuse real Details columns without presenting a partial popup.
    state.columns.clear(); state.columnsView = true;
    for (const auto& key : {PKEY_Size, PKEY_ItemTypeText, PKEY_DateModified, PKEY_Comment, PKEY_Keywords, PKEY_Image_Dimensions}) {
        ExplorerColumn column; column.key = key;
        ComPtr<IPropertyDescription> description; PWSTR label = nullptr;
        Check(SUCCEEDED(PSGetPropertyDescription(key, IID_PPV_ARGS(&description))) &&
            SUCCEEDED(description->GetDisplayName(&label)) && label, "native cached column label");
        column.title = label; CoTaskMemFree(label); state.columns.push_back(std::move(column));
    }
    QueueVisibleDetails(state);
    const int photo = rowOf(L"photo.jpg");
    Check(Eventually([&] {
        ReceiveDirectoryResults(state); QueueVisibleDetails(state);
        return HasCachedColumnCell(state.entries[size_t(photo)], PKEY_Keywords) &&
            HasCachedColumnCell(state.entries[size_t(folder)], PKEY_Size);
    }), "real column cache completion");
    ItemTipHoverMoved(state, -1); state.hovered = photo; ShowItemTip(state, client);
    Check(!shown() && tip.token, "cached tooltip appeared before completion");
    Check(hover(photo) && valueOf(PKEY_Size) == state.entries[size_t(photo)].size &&
        !valueOf(PKEY_Comment).empty() && !valueOf(PKEY_Keywords).empty(), "cached JPEG tooltip completion");
    ItemTipHoverMoved(state, -1); state.hovered = folder; ShowItemTip(state, client);
    Check(!shown() && tip.token, "partial folder tooltip appeared");
    Check(hover(folder) && valueOf(PKEY_Size) == state.entries[size_t(folder)].size,
        "hover provider replaced the cached folder-size column text");

    // Actual monitor edges and midpoint: neither side of the pointer has the
    // whole screen's height available in the middle of the work area.
    Check(hover(photo), "bounded JPEG tooltip");
    for (const POINT anchor : {POINT{monitor.rcWork.left, monitor.rcWork.top},
            POINT{monitor.rcWork.right - 1, monitor.rcWork.bottom - 1},
            POINT{(monitor.rcWork.left + monitor.rcWork.right) / 2, (monitor.rcWork.top + monitor.rcWork.bottom) / 2}}) {
        tip.anchor = anchor;
        Check(LayoutItemTip(state, monitor.rcWork), "bounded tooltip layout");
        const auto at = ItemTipPosition(state, anchor, tip.size, monitor.rcWork);
        Check(at.x >= monitor.rcWork.left && at.y >= monitor.rcWork.top &&
            at.x + tip.size.cx <= monitor.rcWork.right && at.y + tip.size.cy <= monitor.rcWork.bottom &&
            tip.rowLayouts.size() <= tip.rowCapacity, "tooltip crossed the screen work area");
        for (size_t i = 0; i < tip.rowLayouts.size(); ++i) {
            DWRITE_TEXT_METRICS metrics{}; tip.rowLayouts[i].second->GetMetrics(&metrics);
            Check(metrics.lineCount == 1 && tip.rowTops[i] + metrics.height + tip.padY <= tip.size.cy,
                "multiline metadata overlapped or exceeded the tooltip");
        }
    }
    g_detailedItemTips = false;
    ItemTipHoverMoved(state, -1);
    Check(hover(photo) && tip.rows.size() <= 4 && valueOf(PKEY_Comment).empty() && valueOf(PKEY_Keywords).empty(),
        "turning detailed tooltips off retained cached extra fields");

    const HWND tipWindow = tip.hwnd;
    state.itemTip.Release();
    Check(!IsWindow(tipWindow), "item tip teardown left its window");
    SetWindowLongPtrW(view, GWLP_USERDATA, 0);
    const std::weak_ptr<TabBackground> retired = state.background;
    Stop(state.background);
    Check(Eventually([&] { return retired.expired(); }), "item tip lane did not drain");
    }
    DestroyWindow(view);
    Check(UnregisterClassW(kItemTipClass, OverlayModuleInstance()), "item tip class release");
    g_writeFactory.Reset();
    Check(root.parent_path() == std::filesystem::path(directory), "item tip fixture cleanup boundary");
    Check(Eventually([&] {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return !error;
    }), "item tip fixture cleanup");
}

static void TestItemTips(const std::wstring& directory) {
    // SetThreadDesktop requires a thread without windows: use a fresh one.
    std::exception_ptr failure;
    std::thread([&] {
        try {
            const std::wstring name = L"gpu-overlay-itemtip-" + std::to_wstring(GetCurrentProcessId());
            HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
            Check(desktop && SetThreadDesktop(desktop), "private item tip desktop");
            {
                WorkerComApartment apartment;
                ItemTipScenario(directory);
            }
            CloseDesktop(desktop);
        } catch (...) { failure = std::current_exception(); }
    }).join();
    if (failure) std::rethrow_exception(failure);
    std::cout << "PASS: localized basic/file-only detailed tooltips, real JPEG/WAV metadata, column-cache reuse, deduplication, completed-only display, rendering policy, bounds, RTL and cancellation on a private desktop\n";
}
