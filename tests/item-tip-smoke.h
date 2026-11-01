#include <thread>
// Hovered-item tooltip: real popup windows, DirectWrite layout, DWM attributes
// and the tab's Columns lane, on a private desktop that is never displayed.
// Hover is the production entry point after the system hover time; no input
// is injected, and the user's focus, cursor and desktop are untouched.
static void ItemTipScenario(const std::wstring& directory) {
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
    Check(state.total == 3, "item tip fixture row count");
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
        return Eventually([&] {
            MSG message{};
            while (PeekMessageW(&message, view, kMsgItemTipReady, kMsgItemTipReady, PM_REMOVE)) ConsumeItemTip(state);
            return shown() && !tip.token;
        });
    };
    const auto labels = [&] {
        std::wstring joined;
        for (const auto& row : tip.rows) joined += row.label + L";";
        return joined;
    };
    SHFILEINFOW textType{}, folderType{};
    Check(SHGetFileInfoW(L"note.txt", FILE_ATTRIBUTE_NORMAL, &textType, sizeof(textType),
            SHGFI_USEFILEATTRIBUTES | SHGFI_TYPENAME) &&
        SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &folderType, sizeof(folderType),
            SHGFI_USEFILEATTRIBUTES | SHGFI_TYPENAME), "Shell type names");

    // A file: full name, then Size, Type, Created, Modified; Type text came
    // from the lane because this view has no Type column.
    Check(hover(note), "file item tip did not appear");
    const Entry& file = state.entries[size_t(note)];
    Check(tip.name == file.name && labels() == L"Size;Type;Created;Modified;", "file item tip rows");
    Check(tip.rows[0].value == FormatSize(8) && tip.rows[1].value == textType.szTypeName,
        "file item tip size/type differ from Shell");
    Check(tip.rows[2].value.starts_with(FormatFileTime(file.created) + L" \x00B7 ") &&
        tip.rows[3].value.starts_with(FormatFileTime(file.lastWrite) + L" \x00B7 "), "file item tip dates");
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

    // A folder has no size; its type is Shell's folder type.
    Check(hover(folder) && labels() == L"Type;Created;Modified;" && tip.rows[0].value == folderType.szTypeName,
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

    // Relative ages.
    const auto at = [](ULONGLONG seconds) {
        const ULONGLONG ticks = 133000000000000000ull + seconds * 10000000ull;
        return FILETIME{DWORD(ticks), DWORD(ticks >> 32)};
    };
    const FILETIME origin = at(0);
    const std::pair<ULONGLONG, const wchar_t*> ages[]{{30, L"just now"}, {60, L"1 minute ago"},
        {7200, L"2 hours ago"}, {86400, L"1 day ago"}, {8 * 86400, L"1 week ago"},
        {40 * 86400, L"1 month ago"}, {400 * 86400, L"1 year ago"}, {800 * 86400, L"2 years ago"}};
    for (const auto& [seconds, text] : ages)
        Check(RelativeAge(origin, at(seconds)) == text, "item tip relative age");
    Check(RelativeAge(at(30), origin) == L"just now" && RelativeAge(at(60), origin).empty(),
        "slightly fast clocks are just now; later timestamps get no age");

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
    std::cout << "PASS: item tips show full name/size/type/dates with tab-stop columns, RTL, wrapping, theme, dismissal and lane cancellation on a private desktop\n";
}
