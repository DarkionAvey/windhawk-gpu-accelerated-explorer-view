// Explorer-owned naming through the production overlay: Shell file labels
// (Folder Options extension hiding) and multiple-item rename by DefView's own
// editor. Private folders in an offscreen ExplorerBrowser; the user's Folder
// Options are read, never written. Rename error dialogs are substituted.
namespace ShellRenameFixture {
void CreateFiles(const std::filesystem::path& root, const std::vector<std::wstring>& names) {
    std::filesystem::create_directories(root);
    for (const auto& name : names) {
        HANDLE file = CreateFileW((root / name).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, 0, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "private rename fixture file");
        CloseHandle(file);
    }
}
int Row(const ViewState& state, const std::wstring& name) {
    for (size_t i = 0; i < state.entries.size(); ++i)
        if (state.entries[i].name == name) return int(i);
    Check(false, "private rename fixture row");
    return -1;
}
std::wstring Text(HWND window) {
    std::wstring text(size_t(GetWindowTextLengthW(window)) + 1, L'\0');
    text.resize(GetWindowTextW(window, text.data(), int(text.size())));
    return text;
}
// The rename transaction posts these to the overlay; the fixture's static
// overlay HWND forwards them to the production window procedure. The mod
// reads focus through FocusFixture, so DefView's real editor focus is mirrored.
void Pump(ProviderSelectionFixture::Browser& host) {
    auto& state = host.state;
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (const HWND real = ::GetFocus(); real && NativeRenameEditor(state, real)) FocusFixture::focus = real;
        const UINT id = message.message;
        if (message.hwnd == state.hwnd && ((id == WM_TIMER && message.wParam == kClickRenameTimerId) ||
            id == kMsgRenameDecision || id == kMsgNativeRenameStep ||
            id == kMsgNativeRenameEditor || id == kMsgCreationRefocus || id == kMsgPreparedSelection ||
            id == kMsgFlushDirectoryChanges || id == kMsgNativeCatalog))
            OverlayProc(state.hwnd, id, message.wParam, message.lParam);
        else { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    ReceiveDirectoryResults(state);
}
bool HostHidden(const ViewState& state) {
    const HWND host = FolderContentHost(state.nativeHwnd);
    return host && !(GetWindowLongPtrW(host, GWL_STYLE) & WS_VISIBLE);
}
std::wstring Extension(const std::wstring& name) {
    const wchar_t* ext = PathFindExtensionW(name.c_str());
    return ext ? ext : L"";
}
struct Item { std::wstring name; bool folder = false; };
std::vector<Item> Listing(const std::filesystem::path& root) {
    std::vector<Item> items;
    for (const auto& entry : std::filesystem::directory_iterator(root))
        items.push_back({entry.path().filename().wstring(), entry.is_directory()});
    return items;
}
std::wstring SelectedPath(IFolderView2* view) {
    ComPtr<IShellItemArray> items; ComPtr<IShellItem> item; DWORD count = 0; PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(view->GetSelection(FALSE, &items)) && items && SUCCEEDED(items->GetCount(&count)) && count == 1 &&
        SUCCEEDED(items->GetItemAt(0, &item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
        result = path;
    CoTaskMemFree(path);
    return result;
}

enum class Commit { Enter, Row, Empty };

// One F2 group rename through the production overlay. `renamed` files and
// folders form the selection; `kept` items stay unselected. The commit is
// followed by the click's own selection work before DefView's queued commit.
void RenameSet(const std::filesystem::path& root, const std::vector<Item>& renamed,
               const std::vector<std::wstring>& kept, Commit commit) {
    std::vector<std::wstring> files;
    for (const auto& item : renamed) {
        if (item.folder) std::filesystem::create_directories(root / item.name);
        else files.push_back(item.name);
    }
    files.insert(files.end(), kept.begin(), kept.end());
    CreateFiles(root, files);
    {
    ProviderSelectionFixture::Browser host;
    auto& state = host.state;
    FocusFixture::Session focus(host.frame, state.hwnd);
    host.Navigate(root.wstring(), int(renamed.size() + kept.size()));
    Check(HostHidden(state), "native host visible before rename");
    state.selectedFlags.Fill(0);
    for (const auto& item : renamed) state.selectedFlags[size_t(Row(state, item.name))] = 1;
    const int focused = Row(state, renamed.front().name);
    state.selected = state.anchor = focused;
    const bool large = renamed.size() > kMaxDirectNativeSelectionItems;
    BeginRename(state, focused, false, true);
    const auto beganGeneration = state.pendingNativeSelection;
    // A large set reaches Explorer on the selection lane; F2 waits for it.
    Check(!large || !state.rename.editor, "large set opened the editor before Explorer held it");
    const bool opened = Eventually([&] { Pump(host); return state.rename.editor != nullptr; }, 30000);
    if (!opened) {
        int nativeCount = -1; state.folderView->ItemCount(SVGIO_SELECTION, &nativeCount);
        std::cout << "EDITOR WAIT pending=" << state.pendingNativeSelection << " keys=" << state.deferredSelectionKeys.size()
            << " unsafe=" << state.nativeSelectionUnsafe << " import=" << state.nativeSelectionImportPending
            << " awaiting=" << state.nativeSelectionAwaitingRefresh << " confirmed=" << bool(state.confirmedNativeSelection)
            << " gpu=" << state.selectedFlags.Count(0, state.selectedFlags.size()) << " native=" << nativeCount
            << " focused=" << focused << " began=" << beganGeneration << std::endl;
    }
    Check(opened, "F2 did not open the editor for the selected set");
    int nativeSelected = 0;
    Check(state.rename.multiple && SUCCEEDED(state.folderView->ItemCount(SVGIO_SELECTION, &nativeSelected)) &&
        nativeSelected == int(renamed.size()), "editor opened before Explorer held the whole set");
    const std::wstring initial = Text(state.rename.editor);
    Check(initial == EntryLabel(state.entries[size_t(focused)]), "editor did not show Explorer's label");
    const bool hidden = !state.entries[size_t(focused)].label.empty();
    SetWindowTextW(state.rename.editor, (L"x" + initial.substr(1)).c_str());
    FinishRename(state, true);
    if (commit == Commit::Row) Select(state, Row(state, kept.front()));
    if (commit == Commit::Empty) ClearSelectionForFolderMenu(state);
    bool alwaysHidden = true;
    std::vector<Item> items;
    const bool done = Eventually([&] {
        Pump(host); alwaysHidden &= HostHidden(state);
        items = Listing(root);
        const size_t xs = std::count_if(items.begin(), items.end(), [](const Item& i) { return i.name.starts_with(L"x"); });
        return !state.nativeRenameHold && xs == renamed.size() && items.size() == renamed.size() + kept.size();
    }, 60000);
    if (!done) for (const auto& item : items) std::wcout << L"RESULT " << item.name << L"\n";
    Check(done, "Explorer did not rename the whole selected set");
    for (const auto& name : kept)
        Check(std::any_of(items.begin(), items.end(), [&](const Item& i) { return i.name == name; }),
            "unselected item was renamed");
    const auto count = [&](bool folder) {
        return std::count_if(renamed.begin(), renamed.end(), [&](const Item& i) { return i.folder == folder; });
    };
    Check(std::count_if(items.begin(), items.end(), [](const Item& i) { return i.folder && i.name.starts_with(L"x"); }) ==
        count(true), "renamed folders changed kind or count");
    if (hidden) {
        std::vector<std::wstring> before, after;
        for (const auto& item : renamed) if (!item.folder) before.push_back(Extension(item.name));
        for (const auto& item : items) if (!item.folder && item.name.starts_with(L"x")) after.push_back(Extension(item.name));
        std::sort(before.begin(), before.end()); std::sort(after.begin(), after.end());
        Check(before == after, "hidden extensions were not kept");
    }
    Check(alwaysHidden && HostHidden(state), "native view was exposed for the rename");
    if (commit == Commit::Row)
        Check(Eventually([&] {
            Pump(host);
            return !_wcsicmp(SelectedPath(state.folderView.Get()).c_str(), (root / kept.front()).c_str());
        }, 30000), "the commit click's selection did not reach Explorer after the rename");
    if (commit == Commit::Empty)
        Check(Eventually([&] {
            Pump(host); int selected = -1;
            return SUCCEEDED(state.folderView->ItemCount(SVGIO_SELECTION, &selected)) && selected == 0 &&
                !state.selectedFlags.Count(0, state.selectedFlags.size());
        }, 30000), "the empty-space click did not clear Explorer's selection after the rename");
    Check(!state.rename.editor && !NativeRenameEditor(state, ::GetFocus()), "an editor was left open after the rename");
    }
    // Shell objects of the destroyed view release their folder handles from
    // this thread's queue; the renamed set must not stay open.
    Check(Eventually([&] {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        return ProcessHandlesUnder(root).empty();
    }, 30000), "the renamed folder stayed open after its view closed");
}
}

// Labels come from the names worker; the expectation is the Shell folder's
// own in-folder display name for each item, bound independently of the view.
static void TestShellFileLabels(const std::wstring& fixture) {
    using namespace ShellRenameFixture;
    const auto root = std::filesystem::path(fixture) / L"shell-file-labels";
    const std::wstring unknown = L"unknown.zq" + std::to_wstring(GetCurrentProcessId()) + L"q";
    CreateFiles(root, {L"report.txt", L"second.txt", L"UPPER.TXT", L"photo.jpg", L"multi.part.name.txt",
        L"archive.tar.gz", L".gitignore", L"noextension", unknown});
    std::filesystem::create_directory(root / L"folder.txt");
    {
        ComPtr<IShellLinkW> link; ComPtr<IPersistFile> persist;
        Check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) &&
            SUCCEEDED(link->SetPath((root / L"report.txt").c_str())) && SUCCEEDED(link.As(&persist)) &&
            SUCCEEDED(persist->Save((root / L"shortcut.lnk").c_str(), TRUE)), "private shortcut fixture");
    }
    ComPtr<IShellItem> item; ComPtr<IShellFolder> folder;
    Check(SUCCEEDED(SHCreateItemFromParsingName(root.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
        SUCCEEDED(item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&folder))), "independent label folder");
    const auto shellLabel = [&](const std::wstring& name) {
        std::wstring parse = name; ULONG eaten = 0; PIDLIST_RELATIVE raw = nullptr;
        Check(SUCCEEDED(folder->ParseDisplayName(nullptr, nullptr, parse.data(), &eaten, &raw, nullptr)) && raw,
            "independent label item");
        OwnedPidl child(reinterpret_cast<PITEMID_CHILD>(raw));
        STRRET value{}; PWSTR text = nullptr;
        Check(SUCCEEDED(folder->GetDisplayNameOf(child.get(), SHGDN_INFOLDER, &value)) &&
            SUCCEEDED(StrRetToStrW(&value, child.get(), &text)) && text, "independent Shell label");
        std::wstring result = text; CoTaskMemFree(text);
        return result;
    };
    // Folder Options' stored choice, read independently of SHGetSetSettings.
    DWORD hide = 1, bytes = sizeof(hide);
    if (SHRegGetUSValueW(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"HideFileExt",
        nullptr, &hide, &bytes, FALSE, nullptr, 0) != ERROR_SUCCESS || bytes != sizeof(hide)) hide = 1;
    Check(ReadShellVisibilityPolicy().showExtensions == (hide == 0), "visibility policy ignored HideFileExt");

    ProviderSelectionFixture::Browser host;
    auto& state = host.state;
    FocusFixture::Session focus(host.frame, state.hwnd);
    host.Navigate(root.wstring(), 11);
    size_t trimmed = 0;
    for (const auto& entry : state.entries) {
        Regression::Scope scope("item=" + std::string(entry.name.begin(), entry.name.end()));
        const std::wstring expected = shellLabel(entry.name);
        Check(EntryLabel(entry) == expected, "GPU label differs from the Shell's in-folder name");
        Check(entry.label.empty() == (expected == entry.name), "label stored although it equals the filename");
        Check(!entry.folder || entry.label.empty(), "folder name was trimmed like a file extension");
        trimmed += !entry.label.empty();
    }
    // Rows whose only change is their label are a new presentation.
    std::vector<Entry> plain(1), labelled(1);
    plain[0].name = labelled[0].name = L"report.txt";
    plain[0].path = labelled[0].path = (root / L"report.txt").wstring();
    labelled[0].label = L"report";
    Check(DirectoryOrderHash(plain) != DirectoryOrderHash(labelled), "relabelled rows reuse the old snapshot");
    std::cout << "PASS: " << state.entries.size() << " names-worker labels equal Shell in-folder names (" << trimmed
        << " trimmed, HideFileExt=" << hide << "), folders untouched, relabel changes the order hash\n";
}

// F2 on several items: the production editor hands its text to DefView's own
// hidden editor. Each commit (Enter, a click on another row, a click on empty
// space) renames the whole set before the click's selection reaches Explorer.
// The bulk set of 50 files and 50 folders exceeds the direct selection limit,
// so F2 opens the editor only after the selection lane gave Explorer the set.
// Explorer may show its own progress or error UI for a group rename; it runs
// on a private desktop, never on the user's input desktop.
static void TestNativeMultipleRename(const std::wstring& fixture) {
    using namespace ShellRenameFixture;
    wchar_t desktop[256]{}; DWORD length = 0;
    Check(GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, desktop, sizeof(desktop), &length),
        "multiple rename desktop identity");
    if (!std::wstring_view(desktop).starts_with(L"gpu-overlay-multiple-rename-")) {
        PrivateDesktopFixture::Run(L"multiple-rename", L"--test native-multiple-rename", 600000);
        return;
    }
    ReportApiFixture::suppressRenameError = true; ReportApiFixture::renameErrors = 0;
    const auto label = [](Commit commit) {
        return commit == Commit::Enter ? std::wstring(L"enter") : commit == Commit::Row ? L"row" : L"empty";
    };
    for (Commit commit : {Commit::Enter, Commit::Row, Commit::Empty}) {
        Regression::Scope scope("small commit=" + std::to_string(int(commit)));
        RenameSet(std::filesystem::path(fixture) / (L"multiple-rename-" + label(commit)),
            {{L"a.txt"}, {L"b.txt"}, {L"c.jpg"}}, {L"d.txt"}, commit);
    }
    std::vector<Item> bulk;
    for (int i = 0; i < 50; ++i) {
        wchar_t file[32], folder[32];
        swprintf_s(file, L"file%02d%s", i, i % 2 ? L".jpg" : L".txt");
        swprintf_s(folder, L"folder%02d", i);
        bulk.push_back({file}); bulk.push_back({folder, true});
    }
    for (Commit commit : {Commit::Enter, Commit::Row, Commit::Empty}) {
        Regression::Scope scope("bulk commit=" + std::to_string(int(commit)));
        RenameSet(std::filesystem::path(fixture) / (L"bulk-rename-" + label(commit)), bulk,
            {L"keep.txt", L"keep two.txt"}, commit);
    }
    Check(ReportApiFixture::renameErrors == 0, "a rename was reported as failed");
    ReportApiFixture::suppressRenameError = false;
    std::cout << "PASS: group rename by DefView's hidden editor for 3 items and for 50 files + 50 folders, each "
        "committed by Enter, a row click and an empty-space click; large sets open the editor after Explorer "
        "holds them, whole set renamed, kinds and extensions kept, selection held then applied, native view never shown\n";
}

// Explorer's slow second click on the selected item's name. The production
// window procedure receives the press/release, a real overlay timer delivers
// the wake, and the hit target is the filename text, not the Name column.
static void TestClickRename(const std::wstring& fixture) {
    using namespace ShellRenameFixture;
    const auto root = std::filesystem::path(fixture) / L"click-rename";
    CreateFiles(root, {L"alpha.txt", L"beta.txt", L"gamma.txt"});
    ProviderSelectionFixture::Browser host, other;
    auto& state = host.state;
    FocusFixture::Session focus(host.frame, state.hwnd);
    host.Navigate(root.wstring(), 3);
    other.Navigate(root.wstring(), 3);
    const auto select = [&](std::initializer_list<int> rows, int caret) {
        CancelRename(state);
        state.selectedFlags.Fill(0);
        for (int row : rows) state.selectedFlags[size_t(row)] = 1;
        state.selected = state.anchor = caret;
        Check(SyncNativeSelection(state), "click rename selection");
        FocusFixture::focus = state.nativeHwnd; FocusFixture::foreground = host.frame;
    };
    // Independent geometry: the filename-only highlight's own bounds.
    const auto textPoint = [&](int row) {
        SelectionShape shape;
        const auto rc = CurrentSelectionRect(state, row, false, &shape, true);
        const LONG x = LONG((rc.left + rc.right) / 2), y = LONG((rc.top + rc.bottom) / 2);
        return POINT{state.rtlLayout ? LogicalX(state, x) : x, y};
    };
    const auto click = [&](POINT point) {
        OverlayProc(state.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
        OverlayProc(state.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
    };
    const auto settle = [&] { for (int i = 0; i < 4; ++i) Pump(host); };
    const int alpha = Row(state, L"alpha.txt"), beta = Row(state, L"beta.txt");

    // A slow second click on the filename text arms exactly the system
    // double-click time from its release, then opens the editor.
    select({alpha}, alpha);
    const ULONGLONG before = GetTickCount64();
    click(textPoint(alpha));
    const ULONGLONG after = GetTickCount64();
    const UINT wait = GetDoubleClickTime();
    Check(state.clickRename.armed && state.clickRename.row == alpha && !state.rename.editor,
        "slow click on the selected filename did not arm a rename");
    Check(state.clickRename.due >= before + wait && state.clickRename.due <= after + wait,
        "rename wait differs from the double-click time");
    // A wake queued before this arm (KillTimer keeps queued WM_TIMERs) must not open it early.
    if (GetTickCount64() < state.clickRename.due) {
        OverlayProc(state.hwnd, WM_TIMER, kClickRenameTimerId, 0);
        Check(state.clickRename.armed && !state.rename.editor, "an early timer wake opened the rename");
    }
    // Another tab's overlay owns its own timer; its wake renames nothing here.
    OverlayProc(other.state.hwnd, WM_TIMER, kClickRenameTimerId, 0);
    Check(state.clickRename.armed && !other.state.rename.editor, "another tab's timer affected this click");
    Check(Eventually([&] { Pump(host); return state.rename.editor != nullptr; }, 30000),
        "slow click did not open the rename editor");
    Check(state.rename.index == alpha && Text(state.rename.editor) == EntryLabel(state.entries[size_t(alpha)]),
        "slow click renamed a different item");
    CancelRename(state); settle();

    // The Name column outside the filename text selects but never renames.
    select({alpha}, alpha);
    SelectionShape shape;
    const auto text = CurrentSelectionRect(state, alpha, false, &shape, true);
    const int dpi = ViewDpi(state);
    const LONG outsideLogical = LONG(text.right) + Scale96(24, dpi);
    const POINT outside{state.rtlLayout ? LogicalX(state, outsideLogical) : outsideLogical, LONG((text.top + text.bottom) / 2)};
    Check(outsideLogical < LONG(NameColumnEnd(state)) && RowFromPoint(state, outside.y, outside.x) == alpha &&
        !PointOnFilenameText(state, alpha, outside), "fixture point is not in the Name column beside the text");
    click(outside);
    Check(!state.clickRename.armed, "a click beside the filename armed a rename");

    // Any later input supersedes the pending rename.
    select({alpha}, alpha);
    click(textPoint(alpha));
    Check(state.clickRename.armed, "second slow click did not arm");
    OverlayProc(state.hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, 0), MAKELPARAM(textPoint(alpha).x, textPoint(alpha).y));
    Check(!state.clickRename.armed, "wheel input left the rename armed");

    // Identity checked at the wake: a changed selection or lost focus renames nothing.
    for (int scenario = 0; scenario < 2; ++scenario) {
        select({alpha}, alpha);
        click(textPoint(alpha));
        Check(state.clickRename.armed, "slow click did not arm for the stale-identity check");
        if (scenario == 0) { state.selectedFlags.Fill(0); state.selectedFlags[size_t(beta)] = 1; state.selected = state.anchor = beta; }
        else FocusFixture::focus = nullptr;
        const ULONGLONG due = state.clickRename.due;
        Check(Eventually([&] { Pump(host); return GetTickCount64() >= due && !state.clickRename.armed; }, 30000),
            "the pending rename never resolved");
        Check(!state.rename.editor, scenario == 0 ? "a stale selection was renamed" : "a rename opened without input focus");
    }

    // A multiple selection, or a click that only gives the list focus, never renames.
    select({alpha, beta}, alpha);
    click(textPoint(alpha));
    Check(!state.clickRename.armed, "a click in a multiple selection armed a rename");
    select({alpha}, alpha);
    FocusFixture::focus = nullptr;
    click(textPoint(alpha));
    Check(!state.clickRename.armed, "a click that focused the list armed a rename");
    settle();
    Check(!state.rename.editor, "an editor opened without a valid slow click");
    std::cout << "PASS: slow click on the selected filename text opens rename after GetDoubleClickTime() (" << wait
        << " ms) from release; Name column beside the text, multiple selections, focusing clicks, later input, "
        "stale selection/focus, early and foreign-tab timer wakes never rename\n";
}

namespace RenameTextFixture {
struct Canvas {
    HDC dc = nullptr; HBITMAP bitmap = nullptr; HGDIOBJ previous = nullptr; DWORD* bits = nullptr; int width = 0, height = 0;
    Canvas(int w, int h) : width(w), height(h) {
        BITMAPINFO info{}; info.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
        dc = CreateCompatibleDC(nullptr);
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&bits), nullptr, 0);
        Check(dc && bitmap && bits, "rename text canvas");
        previous = SelectObject(dc, bitmap);
    }
    ~Canvas() { SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); }
    // Summed distance from the background colour: what reads as weight.
    double Ink(COLORREF background) const {
        double ink = 0;
        for (int i = 0; i < width * height; ++i) {
            const DWORD c = bits[i];
            ink += std::abs(int((c >> 16) & 255) - int(GetRValue(background))) +
                std::abs(int((c >> 8) & 255) - int(GetGValue(background))) + std::abs(int(c & 255) - int(GetBValue(background)));
        }
        return ink / (3.0 * 255.0);
    }
    // Colour spread per pixel: ClearType's subpixel fringes, absent from
    // grayscale text of a neutral colour.
    double Fringe() const {
        double spread = 0;
        for (int i = 0; i < width * height; ++i) {
            const DWORD c = bits[i];
            const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
            spread += std::max({r, g, b}) - std::min({r, g, b});
        }
        return spread / 255.0;
    }
};
}

// The rename field draws its text like the list: same weight, rasterizer and
// antialiasing (the Text antialiasing option as the list resolves it). The
// expectation is an independent Direct2D rendering with the list's font
// family and rendering parameters, not the mod's own drawing helper.
static void TestRenameTextWeight(const std::wstring& fixture) {
    using namespace ShellRenameFixture;
    using RenameTextFixture::Canvas;
    const auto root = std::filesystem::path(fixture) / L"rename-text-weight";
    const std::wstring name = L"Quarterly report draft (final) 2026.txt";
    CreateFiles(root, {name});
    ProviderSelectionFixture::Browser host;
    auto& state = host.state;
    FocusFixture::Session focus(host.frame, state.hwnd);
    host.Navigate(root.wstring(), 1);
    // The renderer creates the shared DirectWrite factory; this fixture has no renderer.
    const auto factoryBefore = g_writeFactory;
    struct RestoreFactory { ComPtr<IDWriteFactory> value; ~RestoreFactory() { g_writeFactory = value; } } restoreFactory{factoryBefore};
    if (!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))), "rename text fonts");
    Check(MakeTextFormats(state) && state.textFormat, "list text format");
    RefreshTextRendering(state);
    state.selectedFlags.Fill(0); state.selectedFlags[0] = 1; state.selected = state.anchor = 0;
    Check(SyncNativeSelection(state), "rename text selection");
    BeginRename(state, 0);
    const HWND editor = state.rename.editor;
    Check(editor != nullptr, "rename text editor");
    FocusFixture::focus = state.hwnd; // Unfocused: the EDIT shows no selection highlight.
    RECT client{}; GetClientRect(editor, &client);
    const std::wstring text = Text(editor);
    const auto colours = [&](HDC dc) {
        const auto brush = reinterpret_cast<HBRUSH>(SendMessageW(GetParent(editor), WM_CTLCOLOREDIT,
            reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(editor)));
        LOGBRUSH fill{}; GetObjectW(brush, sizeof(fill), &fill);
        return std::pair{fill.lbColor, GetTextColor(dc)};
    };
    // The field's own drawing, and the EDIT's GDI text for comparison.
    Canvas field(client.right, client.bottom), gdi(client.right, client.bottom);
    const auto [background, foreground] = colours(field.dc);
    colours(gdi.dc);
    HBRUSH fill = CreateSolidBrush(background);
    FillRect(field.dc, &client, fill); FillRect(gdi.dc, &client, fill);
    const bool directWrite = PaintRenameText(state, editor, field.dc, client);
    if (!directWrite) std::cout << "FALLBACK " << (state.rename.textFallback ? state.rename.textFallback : "?") << std::endl;
    Check(directWrite, "the field did not draw its text with DirectWrite");
    SendMessageW(editor, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(gdi.dc), PRF_CLIENT); // The EDIT's own GDI text.
    // Independent list-style rendering of the same text at the EDIT's em size.
    TEXTMETRICW metrics{};
    const HGDIOBJ previousFont = SelectObject(gdi.dc, state.rename.font);
    GetTextMetricsW(gdi.dc, &metrics); SelectObject(gdi.dc, previousFont);
    std::wstring family(state.textFormat->GetFontFamilyNameLength() + 1, L'\0');
    state.textFormat->GetFontFamilyName(family.data(), UINT32(family.size()));
    family.resize(wcslen(family.c_str()));
    ComPtr<IDWriteTextFormat> format;
    Check(SUCCEEDED(g_writeFactory->CreateTextFormat(family.c_str(), nullptr, state.textFormat->GetFontWeight(),
        state.textFormat->GetFontStyle(), state.textFormat->GetFontStretch(),
        float(metrics.tmHeight - metrics.tmInternalLeading), L"", &format)), "reference list format");
    ComPtr<ID2D1Factory> factory; ComPtr<ID2D1DCRenderTarget> target; ComPtr<ID2D1SolidColorBrush> ink;
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.f, 96.f);
    Canvas reference(client.right, client.bottom);
    Check(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())) &&
        SUCCEEDED(factory->CreateDCRenderTarget(&properties, &target)) && SUCCEEDED(target->BindDC(reference.dc, &client)) &&
        SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(GetRValue(foreground) / 255.f, GetGValue(foreground) / 255.f,
            GetBValue(foreground) / 255.f), &ink)), "reference list target");
    // The list's transparent surface resolves Windows default to grayscale.
    const auto antialias = state.textAntialiasMode == D2D1_TEXT_ANTIALIAS_MODE_DEFAULT ?
        D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE : state.textAntialiasMode;
    target->BeginDraw();
    target->SetTextRenderingParams(state.textRenderingParams.Get());
    target->SetTextAntialiasMode(antialias);
    target->Clear(D2D1::ColorF(GetRValue(background) / 255.f, GetGValue(background) / 255.f, GetBValue(background) / 255.f));
    target->DrawText(text.c_str(), UINT32(text.size()), format.Get(), D2D1::RectF(4.f, 2.f, 32767.f, float(client.bottom)), ink.Get());
    Check(SUCCEEDED(target->EndDraw()), "reference list rendering");
    DeleteObject(fill);
    const double list = reference.Ink(background), drawn = field.Ink(background), native = gdi.Ink(background);
    const double listFringe = reference.Fringe(), drawnFringe = field.Fringe(), nativeFringe = gdi.Fringe();
    std::cout << "PROFILE rename ink: list=" << list << " field=" << drawn << " gdi=" << native
        << "; colour fringe: list=" << listFringe << " field=" << drawnFringe << " gdi=" << nativeFringe << std::endl;
    Check(std::fabs(drawn - list) <= list * 0.03, "rename text weight differs from the list's");
    Check(drawnFringe <= listFringe * 1.1 + 1.0, "rename text uses different antialiasing than the list");

    // WM_PAINT draws through a buffered-paint DC for an arbitrary update
    // rectangle. Repaint only an offset part that way and compare it with
    // the same pixels of the whole rendering.
    Check(SUCCEEDED(BufferedPaintInit()), "buffered paint");
    Canvas crop(client.right, client.bottom);
    HBRUSH cropFill = CreateSolidBrush(background);
    FillRect(crop.dc, &client, cropFill);
    const RECT part{client.right / 3, 1, client.right - 2, client.bottom - 1};
    HDC buffer = nullptr;
    const HPAINTBUFFER painting = BeginBufferedPaint(crop.dc, &part, BPBF_COMPATIBLEBITMAP, nullptr, &buffer);
    Check(painting && buffer, "buffered field paint");
    colours(buffer);
    FillRect(buffer, &part, cropFill);
    const bool partDrawn = PaintRenameText(state, editor, buffer, part);
    EndBufferedPaint(painting, TRUE);
    BufferedPaintUnInit();
    DeleteObject(cropFill);
    Check(partDrawn, "buffered field paint kept GDI text");
    double partDifference = 0, partInk = 0;
    for (int y = part.top; y < part.bottom; ++y) for (int x = part.left; x < part.right; ++x) {
        const DWORD a = crop.bits[y * crop.width + x], b = field.bits[y * field.width + x];
        for (int shift : {0, 8, 16}) {
            partDifference += std::abs(int((a >> shift) & 255) - int((b >> shift) & 255));
            partInk += std::abs(int((b >> shift) & 255) - int((background >> (16 - shift)) & 255));
        }
    }
    const double painted = crop.Ink(background);
    std::cout << "PROFILE rename buffered part: difference=" << partDifference / 765.0 << " of ink " << partInk / 765.0 << std::endl;
    Check(partInk > 0 && partDifference <= partInk * 0.02 && painted > 0,
        "a buffered partial repaint misplaced the field's text");
    CancelRename(state);
    std::cout << "PASS: rename field text matches the list's DirectWrite weight (ink within 3%) and antialiasing "
        "(no ClearType fringes), including buffered partial repaints\n";
}
