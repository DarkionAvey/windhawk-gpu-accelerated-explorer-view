// Native EDIT layout on private windows/files; no desktop input or focus.
static void TestRenameVerticalAlignment(const std::wstring& fixture) {
    StyleFixture::Session styles;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))), "rename centering fonts");
    for (int dpi : {96, 144, 192}) for (bool rtl : {false, true})
        for (UINT mode : {FVM_DETAILS, FVM_LIST, FVM_SMALLICON}) {
        Regression::Scope context("rename centering dpi=" + std::to_string(dpi) +
            " rtl=" + std::to_string(rtl) + " mode=" + std::to_string(mode));
        StyleFixture::dpi = dpi;
        PresentationFixture::Tab tab(L"rename vertical alignment", 1, rtl, mode == FVM_SMALLICON,
            false, Scale96(600, dpi), Scale96(360, dpi));
        auto& state = tab.state;
        ApplyViewLayout(state, {mode, 48}, false);
        if (rtl) SetWindowLongPtrW(tab.frame, GWL_EXSTYLE,
            GetWindowLongPtrW(tab.frame, GWL_EXSTYLE) | WS_EX_LAYOUTRTL);
        SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
        FocusFixture::Session focus(tab.frame, state.hwnd);
        const auto directory = std::filesystem::path(fixture) /
            (L"center-" + std::to_wstring(dpi) + std::to_wstring(rtl) + std::to_wstring(mode));
        std::filesystem::create_directories(directory);
        state.folderPath = directory.wstring();
        auto& entry = state.entries.front();
        entry.name = L"New Text Document.txt";
        entry.folder = false; entry.path = (directory / entry.name).wstring();
        std::ofstream(std::filesystem::path(entry.path)).close();
        ComPtr<IShellItem> item;
        Check(SUCCEEDED(SHCreateItemFromParsingName(directory.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
            SUCCEEDED(item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&state.folder))),
            "rename centering private Shell folder");
        auto* view = new SelectionFolderView;
        view->folder = state.folder; view->state = &state; view->reenterCapture = false;
        state.folderView.Attach(view); state.selected = state.anchor = 0;
        // A tall row with a smaller caption reproduces the reported gap.
        state.rowHeight96[state.gridView] = 96; UpdateMetrics(state); UpdateColumns(state);
        BeginRename(state, 0);
        Check(state.rename.editor != nullptr, "rename centering editor");
        const HWND editor = state.rename.editor;
        const auto centered = [&] {
            HDC dc = GetDC(editor);
            const auto previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(editor, WM_GETFONT, 0, 0)));
            TEXTMETRICW metrics{};
            Check(GetTextMetricsW(dc, &metrics), "rename centering independent native font metrics");
            SelectObject(dc, previous); ReleaseDC(editor, dc);
            RECT client{}; GetClientRect(editor, &client);
            const LONG lines = LONG(SendMessageW(editor, EM_GETLINECOUNT, 0, 0));
            const LRESULT origin = SendMessageW(editor, EM_POSFROMCHAR, 0, 0);
            const LONG top = static_cast<SHORT>(HIWORD(origin));
            const LONG bottom = top + lines * metrics.tmHeight;
            if (origin == -1 || top < 0 || bottom > client.bottom ||
                std::abs(top - (client.bottom - bottom)) > 1)
                std::cout << "RENAME CENTER client=" << client.bottom << " text=" << top << ".." << bottom
                    << " lines=" << lines << " line-height=" << metrics.tmHeight << std::endl;
            Check(origin != -1 && top >= 0 && bottom <= client.bottom &&
                std::abs(top - (client.bottom - bottom)) <= 1,
                "list rename text block is not vertically centered in its native client");
            Check(SendMessageW(editor, EM_GETFIRSTVISIBLELINE, 0, 0) == 0,
                "centered rename field clipped the filename prefix");
            if (lines * metrics.tmHeight <= state.rowPx - Scale96(4, dpi)) {
                POINT text{0, top}; MapWindowPoints(editor, state.hwnd, &text, 1);
                // Row zero begins below the header. Check its center without
                // calling the production editor-rectangle/alignment helpers.
                Check(std::abs(text.y * 2 + lines * metrics.tmHeight -
                    (state.headerPx * 2 + state.rowPx)) <= 1.f,
                    "rename text and the list row have different vertical centers");
            }
        };
        centered();
        // Mutate through the genuine EDIT APIs: wrap, grow, shrink and undo.
        const std::wstring longName = L"gpu-accelerated-explorer-view with a longer filename that wraps.txt";
        SendMessageW(editor, EM_SETSEL, 0, -1);
        SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(longName.c_str()));
        Check(SendMessageW(editor, EM_GETLINECOUNT, 0, 0) > 1, "centering fixture did not exercise wrapping");
        centered();
        SendMessageW(editor, EM_SETSEL, 0, -1);
        SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"short.txt")); centered();
        SendMessageW(editor, EM_UNDO, 0, 0); centered();
        state.rowHeight96[state.gridView] = 128; UpdateMetrics(state); RepositionRenameEdit(state); centered();
        RenameAppearanceFixture::PaintLayoutProbe stable;
        Check(SetWindowSubclass(editor, RenameAppearanceFixture::ObservePaintLayout, 2,
            reinterpret_cast<DWORD_PTR>(&stable)), "rename centering stable layout observer");
        for (int end : {2, 6, 12}) {
            SendMessageW(editor, EM_SETSEL, 0, end);
            SendMessageW(editor, EM_SCROLLCARET, 0, 0);
            RepositionRenameEdit(state); UpdateWindow(editor);
        }
        RemoveWindowSubclass(editor, RenameAppearanceFixture::ObservePaintLayout, 2);
        Check(!stable.positions && !stable.formats && !stable.fonts,
            "settled centered rename field reformatted during native selection");
        CancelRename(state);
    }
    g_writeFactory.Reset();
    std::cout << "PASS: native list rename text centers across DPI/RTL, wrapping, edits, undo and metrics changes\n";
}
