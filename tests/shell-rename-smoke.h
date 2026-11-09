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
        if (message.hwnd == state.hwnd && ((id == WM_TIMER && (message.wParam == kClickRenameTimerId || message.wParam == kTimerId)) ||
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
    Check(done, "Regression: Explorer's own bulk rename works, but the GPU view's rename did not rename the whole selected set");
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
    {
        // Windows' own bulk rename first. If it fails too, Explorer's Shell is
        // in a stale state (a stuck rename or file operation) and no mod
        // result below would mean anything; a later failure is a regression.
        // A plain Explorer view with no GPU overlay: select two files, start
        // DefView's own label edit, type a name and commit by moving focus.
        const auto probe = std::filesystem::path(fixture) / L"multiple-rename-native";
        CreateFiles(probe, {L"p.txt", L"q.txt"});
        const HWND frame = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"native rename probe",
            WS_POPUP | WS_VISIBLE, -32000, -32000, 640, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        const auto pump = [] { MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message); } };
        ComPtr<IExplorerBrowser> browser; ComPtr<IFolderView2> view;
        RECT bounds{0, 0, 640, 400}; FOLDERSETTINGS settings{FVM_DETAILS, FWF_NOBROWSERVIEWSTATE};
        PIDLIST_ABSOLUTE folder = nullptr;
        bool renamed = frame && SUCCEEDED(CoCreateInstance(CLSID_ExplorerBrowser, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&browser))) && SUCCEEDED(browser->Initialize(frame, &bounds, &settings)) &&
            SUCCEEDED(browser->SetOptions(EBO_NOBORDER | EBO_NOTRAVELLOG)) &&
            SUCCEEDED(SHParseDisplayName(probe.c_str(), nullptr, &folder, 0, nullptr)) &&
            SUCCEEDED(browser->BrowseToIDList(folder, SBSP_ABSOLUTE));
        CoTaskMemFree(folder);
        renamed = renamed && Eventually([&] {
            pump(); int count = 0;
            return SUCCEEDED(browser->GetCurrentView(IID_PPV_ARGS(&view))) && view &&
                SUCCEEDED(view->ItemCount(SVGIO_ALLVIEW, &count)) && count == 2;
        }) && SUCCEEDED(view->SelectItem(0, SVSI_SELECT | SVSI_DESELECTOTHERS | SVSI_FOCUSED)) &&
            SUCCEEDED(view->SelectItem(1, SVSI_SELECT)) && SUCCEEDED(view->DoRename());
        HWND editor = nullptr;
        renamed = renamed && Eventually([&] {
            pump();
            EnumChildWindows(frame, [](HWND child, LPARAM out) {
                wchar_t name[16]{};
                if (GetClassNameW(child, name, ARRAYSIZE(name)) && !_wcsicmp(name, L"Edit") && IsWindowVisible(child)) {
                    *reinterpret_cast<HWND*>(out) = child; return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&editor));
            return editor != nullptr;
        });
        if (renamed) { SetWindowTextW(editor, L"x"); SetFocus(frame); }
        renamed = renamed && Eventually([&] {
            pump();
            const auto listing = Listing(probe);
            return listing.size() == 2 && std::all_of(listing.begin(), listing.end(),
                [](const Item& i) { return i.name.starts_with(L"x"); });
        }, 20000);
        view.Reset();
        if (browser) browser->Destroy();
        browser.Reset();
        if (frame) DestroyWindow(frame);
        Check(renamed, "Explorer is stale: Windows' own multi-item rename also failed. Restart Explorer (or sign out), "
            "check that renaming several files works in native Explorer, then rerun");
    }
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

namespace RenameTextFixture {
// Columns/rows holding ink (any channel away from the background).
struct Extent {
    int left = -1, right = -1, top = -1, bottom = -1;
    int Width() const { return right - left + 1; }
    int Height() const { return bottom - top + 1; }
};
inline Extent InkExtent(const Canvas& canvas, COLORREF background) {
    Extent e;
    for (int y = 0; y < canvas.height; ++y) for (int x = 0; x < canvas.width; ++x) {
        const DWORD c = canvas.bits[y * canvas.width + x];
        const int d = std::abs(int((c >> 16) & 255) - int(GetRValue(background))) +
            std::abs(int((c >> 8) & 255) - int(GetGValue(background))) + std::abs(int(c & 255) - int(GetBValue(background)));
        if (d < 96) continue; // Ignore faint antialiasing at the glyph ends.
        if (e.left < 0 || x < e.left) e.left = x;
        if (x > e.right) e.right = x;
        if (e.top < 0) e.top = y;
        e.bottom = y;
    }
    return e;
}
}

// A long name in the rename field has the list's size and spacing. The
// expectation is the list's own text format in a natural DirectWrite layout,
// drawn independently, in details and list views, at the configured and a
// zoomed (fractional) size, with Explorer's font and custom families.
static void TestRenameTextMetrics(const std::wstring& fixture) {
    using namespace ShellRenameFixture;
    using RenameTextFixture::Canvas;
    const std::wstring name = L"Quarterly financial report draft for the regional board meeting 2026.txt"; // Long, yet one line in Courier New zoomed.
    const auto root = std::filesystem::path(fixture) / L"rename-text-metrics";
    CreateFiles(root, {name});
    auto values = OptionCatalog::Defaults();
    values[L"Appearance.rowHeight"] = L"32";
    values[L"Appearance.fontSizePercent"] = L"52";
    values[L"Appearance.gridFontSizePercent"] = L"42";
    struct Restore { ~Restore() { SettingsFixture::values = nullptr; } } restore;
    SettingsFixture::values = &values;
    const auto factoryBefore = g_writeFactory;
    struct RestoreFactory { ComPtr<IDWriteFactory> value; ~RestoreFactory() { g_writeFactory = value; } } restoreFactory{factoryBefore};
    if (!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))), "rename metric fonts");
    std::vector<std::wstring> families{L"", L"Courier New"};
    {
        ComPtr<IDWriteFontCollection> fonts; UINT32 index = 0; BOOL exists = FALSE;
        if (SUCCEEDED(g_writeFactory->GetSystemFontCollection(&fonts)) &&
            SUCCEEDED(fonts->FindFamilyName(L"Arial Nova", &index, &exists)) && exists) families.push_back(L"Arial Nova");
    }
    ProviderSelectionFixture::Browser host;
    auto& state = host.state;
    FocusFixture::Session focus(host.frame, state.hwnd);
    host.Navigate(root.wstring(), 1);
    bool failed = false;
    for (const auto& family : families) for (UINT mode : {UINT(FVM_DETAILS), UINT(FVM_LIST)}) for (bool zoomed : {false, true}) {
        values[L"Appearance.fontFamily"] = family;
        LoadSettings();
        ApplyViewLayout(state, {mode, 48}, false); UpdateColumns(state);
        state.rowHeight96[0] = zoomed ? 35 : 0; state.preciseRowHeight96[0] = zoomed ? 34.6 : 0.; // Fractional list size.
        UpdateMetrics(state);
        Check(MakeTextFormats(state) && state.textFormat, "rename metric list format");
        RefreshTextRendering(state);
        state.selectedFlags.Fill(0); state.selectedFlags[0] = 1; state.selected = state.anchor = 0;
        Check(SyncNativeSelection(state), "rename metric selection");
        BeginRename(state, 0);
        const HWND editor = state.rename.editor;
        Check(editor != nullptr, "rename metric editor");
        FocusFixture::focus = state.hwnd;
        RECT client{}; GetClientRect(editor, &client);
        const std::wstring text = Text(editor);
        const int width = std::max<int>(client.right, 2400);
        Canvas field(width, client.bottom), gdi(width, client.bottom), list(width, client.bottom);
        const auto brush = reinterpret_cast<HBRUSH>(SendMessageW(GetParent(editor), WM_CTLCOLOREDIT,
            reinterpret_cast<WPARAM>(field.dc), reinterpret_cast<LPARAM>(editor)));
        LOGBRUSH fill{}; GetObjectW(brush, sizeof(fill), &fill);
        const COLORREF background = fill.lbColor, foreground = GetTextColor(field.dc);
        SendMessageW(GetParent(editor), WM_CTLCOLOREDIT, reinterpret_cast<WPARAM>(gdi.dc), reinterpret_cast<LPARAM>(editor));
        HBRUSH solid = CreateSolidBrush(background);
        RECT all{0, 0, width, client.bottom};
        FillRect(field.dc, &all, solid); FillRect(gdi.dc, &all, solid); DeleteObject(solid);
        const bool directWrite = PaintRenameText(state, editor, field.dc, client);
        SendMessageW(editor, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(gdi.dc), PRF_CLIENT);
        // The list's own format and natural layout, drawn independently.
        ComPtr<ID2D1Factory> factory; ComPtr<ID2D1DCRenderTarget> target; ComPtr<ID2D1SolidColorBrush> ink;
        ComPtr<IDWriteTextLayout> layout;
        const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.f, 96.f);
        Check(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())) &&
            SUCCEEDED(factory->CreateDCRenderTarget(&properties, &target)) && SUCCEEDED(target->BindDC(list.dc, &all)) &&
            SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(GetRValue(foreground) / 255.f, GetGValue(foreground) / 255.f,
                GetBValue(foreground) / 255.f), &ink)) &&
            SUCCEEDED(g_writeFactory->CreateTextLayout(text.c_str(), UINT32(text.size()), state.textFormat.Get(),
                100000.f, float(client.bottom), &layout)), "rename metric list layout");
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        target->BeginDraw();
        target->SetTextRenderingParams(state.textRenderingParams.Get());
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        target->Clear(D2D1::ColorF(GetRValue(background) / 255.f, GetGValue(background) / 255.f, GetBValue(background) / 255.f));
        target->DrawTextLayout(D2D1::Point2F(4.f, 1.f), layout.Get(), ink.Get());
        Check(SUCCEEDED(target->EndDraw()), "rename metric list rendering");
        const auto l = RenameTextFixture::InkExtent(list, background), f = RenameTextFixture::InkExtent(field, background),
            g = RenameTextFixture::InkExtent(gdi, background);
        std::wcout << L"METRICS family=" << (family.empty() ? L"(Explorer)" : family) << L" view="
            << (mode == FVM_DETAILS ? L"details" : L"list") << L" zoomed=" << zoomed << L" fontPx=" << state.fontPx
            << L" field-client=" << client.right << L" directWrite=" << directWrite
            << L" | width list=" << l.Width() << L" field=" << f.Width() << L" gdi=" << g.Width()
            << L" | height list=" << l.Height() << L" field=" << f.Height() << L" gdi=" << g.Height() << std::endl;
        const bool oneLine = SendMessageW(editor, EM_GETLINECOUNT, 0, 0) == 1;
        failed |= !oneLine || !directWrite || std::abs(f.Width() - l.Width()) > 1 || std::abs(f.Height() - l.Height()) > 1 ||
            f.right >= client.right;
        // The caret and the pointer follow the displayed text: a boundary of
        // the list's own layout, not the EDIT's GDI advances.
        ::SetFocus(editor); FocusFixture::focus = editor;
        const auto boundary = [&](UINT32 k) {
            DWRITE_HIT_TEST_METRICS hit{}; FLOAT x = 0.f, y = 0.f;
            layout->HitTestTextPosition(k, FALSE, &x, &y, &hit);
            return x;
        };
        SendMessageW(editor, EM_SETSEL, 0, 0);
        POINT origin{}; GetCaretPos(&origin);
        int caretError = 0, clickError = 0;
        for (UINT32 k : {UINT32(text.size() / 4), UINT32(text.size() / 2), UINT32(text.size() * 3 / 4), UINT32(text.size() - 1)}) {
            const int expected = int(std::lround(boundary(k) - boundary(0)));
            SendMessageW(editor, EM_SETSEL, k, k);
            POINT caret{}; GetCaretPos(&caret);
            caretError = std::max(caretError, std::abs(int(caret.x - origin.x) - expected));
            const LPARAM at = MAKELPARAM(origin.x + expected + 1, caret.y + 2);
            SendMessageW(editor, WM_LBUTTONDOWN, MK_LBUTTON, at);
            SendMessageW(editor, WM_LBUTTONUP, 0, at);
            DWORD selectionStart = 0, selectionEnd = 0;
            SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<LPARAM>(&selectionEnd));
            clickError = std::max(clickError, std::abs(int(selectionStart) - int(k)) + std::abs(int(selectionEnd) - int(k)));
        }
        std::wcout << L"  caret error=" << caretError << L" px, click error=" << clickError << L" characters" << std::endl;
        failed |= caretError > 1 || clickError != 0;
        FocusFixture::focus = state.hwnd;
        CancelRename(state);
    }
    Check(!failed, "the rename field's text size, spacing, caret or pointer differs from the list's text");
    std::cout << "PASS: long names in the rename field match the list's text width and height in details and list views, "
        "configured and fractional zoomed sizes, Explorer's and custom font families; caret and clicks follow the shown text\n";
}

// A new item enters rename immediately, as in Explorer, for each creation
// route: the background menu's New > Folder, Ctrl+Shift+N (its recorded
// intent), the command bar (focus on another control when the command runs)
// and a grouped view. All of them run the Shell's New menu object; the
// production hook on it, the creation notification and the overlay do the rest.
static void TestCreatedItemRename(const std::wstring& fixture) {
    using namespace ShellRenameFixture;
    enum class Route { Menu, Shortcut, CommandBar, Grouped };
    const auto label = [](Route route) {
        return route == Route::Menu ? std::wstring(L"menu") : route == Route::Shortcut ? L"shortcut" :
            route == Route::CommandBar ? L"command-bar" : L"grouped";
    };
    // Stand-in for the Windhawk hook: the production detour on the address
    // the mod hooks, patched into the New menu object's shared vtable.
    void* const target = NewMenuInvokeCommandAddress();
    ComPtr<IContextMenu> newMenu;
    Check(target && SUCCEEDED(CoCreateInstance(CLSID_NewMenu, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&newMenu))),
        "Shell New menu object");
    void** const slot = &(*reinterpret_cast<void***>(newMenu.Get()))[4];
    Check(*slot == target, "New menu command address differs from its vtable");
    const auto patch = [&](void* value) {
        DWORD old = 0; VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old);
        *slot = value; VirtualProtect(slot, sizeof(void*), old, &old);
    };
    g_newMenuInvokeOriginal = reinterpret_cast<NewMenuInvoke_t>(target);
    patch(reinterpret_cast<void*>(&NewMenuInvokeHook));
    struct Unpatch { std::function<void()> undo; ~Unpatch() { undo(); } } unpatch{[&] { patch(target); }};
    bool failed = false;
    for (Route route : {Route::Menu, Route::Shortcut, Route::CommandBar, Route::Grouped}) {
        Regression::Scope scope("route=" + std::to_string(int(route)));
        const auto root = std::filesystem::path(fixture) / (L"created-" + label(route));
        CreateFiles(root, {L"alpha.txt", L"beta.jpg", L"gamma.txt"});
        ProviderSelectionFixture::Browser host(true);
        auto& state = host.state;
        FocusFixture::Session focus(host.frame, state.hwnd);
        host.Navigate(root.wstring(), 3);
        SetTimer(state.hwnd, kTimerId, 250, nullptr); // The active overlay's idle timer.
        const DWORD thread = GetCurrentThreadId();
        const HWINEVENTHOOK lifecycle = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_REORDER, nullptr,
            ShellViewEvent, GetCurrentProcessId(), thread, WINEVENT_OUTOFCONTEXT);
        const HWINEVENTHOOK focusEvents = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_SELECTIONWITHIN, nullptr,
            ShellViewEvent, GetCurrentProcessId(), thread, WINEVENT_OUTOFCONTEXT);
        Check(lifecycle && focusEvents, "created item event hooks");
        if (route == Route::Grouped) {
            Check(SUCCEEDED(state.folderView->SetGroupBy(PKEY_ItemTypeText, TRUE)), "group by type");
            RefreshSortFromExplorer(state, true, true);
            Check(Eventually([&] { Pump(host); return state.grouping.enabled && !state.directoryPending && state.loaded; }, 30000),
                "grouped view did not settle");
        }
        HWND commandBar = nullptr;
        ::SetFocus(state.nativeHwnd); FocusFixture::focus = state.nativeHwnd;
        if (route == Route::CommandBar) {
            commandBar = CreateWindowExW(0, L"BUTTON", L"New", WS_CHILD | WS_VISIBLE, 0, 0, 60, 24,
                host.frame, nullptr, GetModuleHandleW(nullptr), nullptr);
            ::SetFocus(commandBar); FocusFixture::focus = commandBar;
        }
        if (route == Route::Shortcut) ArmInteractiveCreation(state, 1, L"Ctrl+Shift+N");
        // Explorer's own New > Folder command on this view's background.
        ComPtr<IShellView> view; ComPtr<IContextMenu> menu;
        Check(SUCCEEDED(state.folderView.As(&view)) &&
            SUCCEEDED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&menu))), "background menu");
        const HMENU popup = CreatePopupMenu();
        Check(SUCCEEDED(menu->QueryContextMenu(popup, 0, 1, 0x7FFF, CMF_NORMAL)), "background menu items");
        CMINVOKECOMMANDINFOEX invoke{sizeof(invoke)};
        invoke.fMask = CMIC_MASK_UNICODE; invoke.hwnd = host.frame; invoke.nShow = SW_SHOWNORMAL;
        invoke.lpVerb = CMDSTR_NEWFOLDERA; invoke.lpVerbW = CMDSTR_NEWFOLDERW;
        const HRESULT created = menu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&invoke));
        DestroyMenu(popup);
        Check(SUCCEEDED(created), "New Folder command");
        const bool opened = Eventually([&] { Pump(host); return state.rename.editor != nullptr; }, 15000);
        std::wstring createdName;
        for (const auto& item : std::filesystem::directory_iterator(root))
            if (item.is_directory()) createdName = item.path().filename().wstring();
        const HWND nativeEditor = FindWindowExW(FolderContentHost(state.nativeHwnd), nullptr, L"Edit", nullptr);
        HWND realFocus = ::GetFocus(); wchar_t focusClass[48]{}; if (realFocus) GetClassNameW(realFocus, focusClass, 48);
        const int row = createdName.empty() ? -1 : GpuRowForNativePath(state, (root / createdName).wstring());
        std::wcout << L"CREATED route=" << label(route) << L" folder=" << createdName << L" editor=" << opened
            << L" row=" << row << L" rows=" << state.total << L" loaded=" << state.loaded << L" pending=" << state.directoryPending
            << L" nativeEditor=" << (nativeEditor != nullptr) << L" focus=" << focusClass
            << L" nativeFocused=" << NativeRenameEditor(state, realFocus)
            << L" editingIndex=" << state.rename.index << std::endl;
        failed |= !opened || createdName.empty() || state.rename.index != row;
        if (opened) {
            // The field's item is drawn selected even while focus is elsewhere.
            const HWND previous = FocusFixture::focus;
            FocusFixture::focus = nullptr;
            const bool shownSelected = SelectionFeedbackVisible(state) && state.rename.index >= 0 &&
                state.selectedFlags[size_t(state.rename.index)];
            FocusFixture::focus = previous;
            if (!shownSelected) { int nativeCount = -1; state.folderView->ItemCount(SVGIO_SELECTION, &nativeCount); std::wcout << L"  field item not shown selected: gpuSelected=" << state.selectedFlags.Count(0, state.selectedFlags.size()) << L" caret=" << state.selected << L" index=" << state.rename.index << L" native=" << nativeCount << L" importPending=" << state.nativeSelectionImportPending << L" unsafe=" << state.nativeSelectionUnsafe << std::endl; }
            failed |= !shownSelected;
        }
        if (opened && route == Route::Grouped) {
            // Toggling the item's section is input outside the field: it commits.
            SetWindowTextW(state.rename.editor, L"toggled");
            ToggleGroupSection(state, state.rowGroup[size_t(state.rename.index)]);
            const bool committed = !state.rename.editor &&
                Eventually([&] { Pump(host); return std::filesystem::exists(root / L"toggled"); }, 15000);
            if (!committed) std::wcout << L"  section toggle did not commit the field" << std::endl;
            failed |= !committed;
        }
        CancelRename(state);
        for (int i = 0; i < 4; ++i) Pump(host);
        UnhookWinEvent(lifecycle); UnhookWinEvent(focusEvents);
        if (commandBar) DestroyWindow(commandBar);
    }
    Check(!failed, "a newly created item did not enter rename");
    std::cout << "PASS: New > Folder, Ctrl+Shift+N, the command bar and a grouped view each open the new item's "
        "rename field\n";
}

namespace RenameTypingFixture {
inline thread_local HWND counted = nullptr;
inline thread_local unsigned directDraws = 0;
inline HDC WINAPI CountingGetDC(HWND hwnd) { if (hwnd && hwnd == counted) ++directDraws; return ::GetDC(hwnd); }
// The physical pointer, where a real click on the field is.
inline thread_local POINT cursor{};
inline BOOL WINAPI CursorAt(LPPOINT point) { if (!point) return FALSE; *point = cursor; return TRUE; }
inline DWORD WINAPI MessageAt() { return MAKELONG(static_cast<short>(cursor.x), static_cast<short>(cursor.y)); }
// Point one module's imported functions at replacements (as a function hook
// on them would), returning the slots patched so they can be restored.
inline std::vector<std::pair<void**, void*>> PatchImports(HMODULE module, const std::map<std::string, void*>& replacements) {
    std::vector<std::pair<void**, void*>> patched;
    auto base = reinterpret_cast<BYTE*>(module);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const auto& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress) return patched;
    for (auto d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress); d->Name; ++d) {
        if (!d->OriginalFirstThunk) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->OriginalFirstThunk);
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const char* name = reinterpret_cast<const char*>(reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name);
            const auto found = replacements.find(name);
            if (found == replacements.end()) continue;
            void** slot = reinterpret_cast<void**>(&slots->u1.Function);
            DWORD old = 0; VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old);
            patched.push_back({slot, *slot}); *slot = found->second;
            VirtualProtect(slot, sizeof(void*), old, &old);
        }
    }
    return patched;
}
inline void Restore(const std::vector<std::pair<void**, void*>>& patched) {
    for (const auto& [slot, value] : patched) {
        DWORD old = 0; VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old);
        *slot = value; VirtualProtect(slot, sizeof(void*), old, &old);
    }
}
}

// Typing in the rename field shows only the list-matched text: Explorer's
// themed EDIT (COMCTL32 v6) draws typed text and drag selections through
// GetDC, which goes offscreen while the field paints itself, so no GDI
// glyphs flash between keystrokes. The hooks are installed on that module's
// own imports, as Windhawk's hook on GetDC/ReleaseDC intercepts them.
static void TestRenameTyping(const std::wstring& fixture) {
    using namespace ShellRenameFixture;
    const auto root = std::filesystem::path(fixture) / L"rename-typing";
    CreateFiles(root, {L"Quarterly report draft.txt"});
    ProviderSelectionFixture::Browser host;
    auto& state = host.state;
    FocusFixture::Session focus(host.frame, state.hwnd);
    host.Navigate(root.wstring(), 1);
    const auto factoryBefore = g_writeFactory;
    struct RestoreFactory { ComPtr<IDWriteFactory> value; ~RestoreFactory() { g_writeFactory = value; } } restoreFactory{factoryBefore};
    if (!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))), "rename typing fonts");
    Check(MakeTextFormats(state) && state.textFormat, "rename typing list format");
    // Explorer's own EDIT class: COMCTL32 v6 through its manifest.
    const auto manifest = std::filesystem::path(__FILE__).parent_path() / L"rename-native-controls.manifest";
    ACTCTXW settings{sizeof(settings)}; settings.lpSource = manifest.c_str();
    const HANDLE context = CreateActCtxW(&settings);
    ULONG_PTR cookie = 0;
    Check(context != INVALID_HANDLE_VALUE && ActivateActCtx(context, &cookie), "themed native controls");
    state.selectedFlags.Fill(0); state.selectedFlags[0] = 1; state.selected = state.anchor = 0;
    Check(SyncNativeSelection(state), "rename typing selection");
    BeginRename(state, 0);
    DeactivateActCtx(0, cookie); ReleaseActCtx(context);
    const HWND editor = state.rename.editor;
    Check(editor != nullptr, "rename typing editor");
    HMODULE editModule = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(GetClassLongPtrW(editor, GCLP_WNDPROC)), &editModule);
    wchar_t modulePath[MAX_PATH]{}; GetModuleFileNameW(editModule, modulePath, MAX_PATH);
    Check(StrStrIW(modulePath, L"comctl32") != nullptr, "the rename field is not Explorer's COMCTL32 EDIT");
    // Stand-in for the Windhawk hooks: production detours, counted originals.
    RenameTypingFixture::counted = editor;
    g_getDcOriginal = &RenameTypingFixture::CountingGetDC;
    g_releaseDcOriginal = &::ReleaseDC;
    g_renameSinkAvailable = true;
    const auto patched = RenameTypingFixture::PatchImports(editModule,
        {{"GetDC", reinterpret_cast<void*>(&GetDcHook)}, {"ReleaseDC", reinterpret_cast<void*>(&ReleaseDcHook)}});
    struct Unpatch { std::vector<std::pair<void**, void*>> slots; ~Unpatch() {
        RenameTypingFixture::Restore(slots); g_renameSinkAvailable = false; RenameTypingFixture::counted = nullptr; } }
        unpatch{patched};
    Check(patched.size() >= 2, "COMCTL32 GetDC/ReleaseDC imports");
    ::SetFocus(editor); FocusFixture::focus = editor;
    SendMessageW(editor, EM_SETSEL, 9, 9);
    RedrawWindow(editor, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    Check(t_renameSinkWindow == editor && t_renameSink, "the field's paint did not take over the EDIT's drawing");
    RenameTypingFixture::directDraws = 0;
    for (wchar_t c : std::wstring(L" final")) SendMessageW(editor, WM_CHAR, c, 0);
    SendMessageW(editor, WM_KEYDOWN, VK_LEFT, 0); SendMessageW(editor, WM_KEYUP, VK_LEFT, 0);
    SendMessageW(editor, WM_KEYDOWN, VK_BACK, 0); SendMessageW(editor, WM_CHAR, VK_BACK, 0);
    const std::wstring typed = Text(editor);
    std::wcout << L"TYPING text='" << typed << L"' direct EDIT draws=" << RenameTypingFixture::directDraws << std::endl;
    Check(typed == L"Quarterly finl report draft" || typed == L"Quarterly fina report draft" ||
        typed.find(L"fin") != std::wstring::npos, "typing did not reach the EDIT");
    Check(RenameTypingFixture::directDraws == 0, "the EDIT drew GDI text on screen while typing");
    // The caret still sits on the shown text after typing.
    DWORD start = 0, end = 0;
    SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    POINT caret{}; GetCaretPos(&caret);
    const auto* line = RenameTextLineAt(state.rename, caret.y);
    Check(line && std::abs(caret.x - LONG(std::lround(RenameDisplayedX(*line, int(end))))) <= 1,
        "the caret left the shown text while typing");
    // Placing and measuring the field inside the EDIT's own drawing (as a
    // click does) leaves the EDIT's font on the DC it holds.
    {
        const HDC held = GetDcHook(editor);
        const auto editFont = reinterpret_cast<HFONT>(SendMessageW(editor, WM_GETFONT, 0, 0));
        Check(held == t_renameSink && editFont, "rename typing held DC");
        SelectObject(held, editFont);
        RenameEditorRect(state, state.rename.index);
        state.rename.textLines.clear();
        BuildRenameTextLines(state, editor);
        const bool kept = GetCurrentObject(held, OBJ_FONT) == editFont;
        ReleaseDcHook(editor, held);
        Check(kept, "a nested field measurement reset the EDIT's font on its DC");
    }
    FocusFixture::focus = state.hwnd;
    CancelRename(state);
    Check(!t_renameSink, "the EDIT's drawing stayed redirected after the field closed");
    std::cout << "PASS: typing in the rename field never draws the EDIT's GDI text on screen; the field paints "
        "each edit and the caret stays on the shown text\n";
}

// The rename field edits like Explorer's own rename box: Ctrl+Backspace and
// the other standard editing shortcuts travel the real TranslateMessage /
// DispatchMessageW path, so Windows generates the WM_CHARs (U+007F for
// Ctrl+Backspace) and the field's native and Shell handlers act on them.
static void TestRenameEditingShortcuts(const std::wstring& fixture) {
    using namespace ShellRenameFixture;
    const auto root = std::filesystem::path(fixture) / L"rename-editing-shortcuts";
    CreateFiles(root, {L"PhotoCraft Notes draft.txt"});
    ProviderSelectionFixture::Browser host;
    auto& state = host.state;
    FocusFixture::Session focus(host.frame, state.hwnd);
    host.Navigate(root.wstring(), 1);
    const auto factoryBefore = g_writeFactory;
    struct RestoreFactory { ComPtr<IDWriteFactory> value; ~RestoreFactory() { g_writeFactory = value; } } restoreFactory{factoryBefore};
    if (!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))), "rename shortcut fonts");
    Check(MakeTextFormats(state) && state.textFormat, "rename shortcut list format");
    // Explorer's own EDIT class: COMCTL32 v6 through its manifest.
    const auto manifest = std::filesystem::path(__FILE__).parent_path() / L"rename-native-controls.manifest";
    ACTCTXW settings{sizeof(settings)}; settings.lpSource = manifest.c_str();
    const HANDLE context = CreateActCtxW(&settings);
    ULONG_PTR cookie = 0;
    Check(context != INVALID_HANDLE_VALUE && ActivateActCtx(context, &cookie), "themed native controls");
    state.selectedFlags.Fill(0); state.selectedFlags[0] = 1; state.selected = state.anchor = 0;
    Check(SyncNativeSelection(state), "rename shortcut selection");
    BeginRename(state, 0);
    DeactivateActCtx(0, cookie); ReleaseActCtx(context);
    const HWND editor = state.rename.editor;
    Check(editor != nullptr, "rename shortcut editor");
    ::SetFocus(editor); FocusFixture::focus = editor;
    // Modifiers live in this thread's own key state, which every handler's
    // GetKeyState reads; the user's keyboard and other processes are untouched.
    BYTE saved[256]{};
    Check(GetKeyboardState(saved) != FALSE, "keyboard state snapshot");
    struct RestoreKeys { BYTE* keys; ~RestoreKeys() { SetKeyboardState(keys); } } restoreKeys{saved};
    const auto press = [&](UINT vk, bool control, bool shift) {
        BYTE keys[256]{};
        if (control) keys[VK_CONTROL] = keys[VK_LCONTROL] = 0x80;
        if (shift) keys[VK_SHIFT] = keys[VK_LSHIFT] = 0x80;
        const bool extended = vk == VK_LEFT || vk == VK_RIGHT || vk == VK_HOME || vk == VK_END || vk == VK_DELETE;
        const LPARAM down = LPARAM(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16) | (extended ? 1 << 24 : 0) | 1;
        for (const bool pressed : {true, false}) {
            keys[vk] = pressed ? 0x80 : 0;
            Check(SetKeyboardState(keys) && PostMessageW(editor, pressed ? WM_KEYDOWN : WM_KEYUP, vk,
                pressed ? down : down | (LPARAM(3) << 30)), "post editing key");
            MSG msg{};
            while (PeekMessageW(&msg, editor, 0, 0, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        Check(SetKeyboardState(saved) != FALSE, "restore keyboard state");
    };
    using Range = std::pair<DWORD, DWORD>;
    const auto selection = [&] {
        DWORD start = 0, end = 0;
        SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        return Range{start, end};
    };
    const auto clean = [&](const char* step) {
        const std::wstring text = Text(editor);
        const Range range = selection();
        std::wcout << L"SHORTCUT " << step << L" text='" << text << L"' sel=" << range.first << L"-"
            << range.second << std::endl;
        Check(std::none_of(text.begin(), text.end(), [](wchar_t c) { return c < 0x20 || c == 0x7f; }),
            (std::string(step) + " inserted a control character into the name").c_str());
        return text;
    };
    const std::wstring original = L"PhotoCraft Notes draft";
    const DWORD length = DWORD(original.size());
    SetWindowTextW(editor, original.c_str());
    SendMessageW(editor, EM_SETSEL, length, length);
    // Word movement and word selection.
    press(VK_LEFT, true, false); clean("Ctrl+Left");
    Check(selection() == Range{17, 17}, "Ctrl+Left did not move to the previous word");
    press(VK_LEFT, true, false); clean("Ctrl+Left twice");
    Check(selection() == Range{11, 11}, "Ctrl+Left did not move by words");
    press(VK_RIGHT, true, false); clean("Ctrl+Right");
    Check(selection() == Range{17, 17}, "Ctrl+Right did not move to the next word");
    press(VK_LEFT, true, true); clean("Ctrl+Shift+Left");
    Check(selection() == Range{11, 17}, "Ctrl+Shift+Left did not select the previous word");
    // Line movement and selection.
    press(VK_HOME, false, false); clean("Home");
    Check(selection() == Range{0, 0}, "Home did not move to the start");
    press(VK_RIGHT, true, true); clean("Ctrl+Shift+Right");
    Check(selection() == Range{0, 11}, "Ctrl+Shift+Right did not select the next word");
    press(VK_END, false, true); clean("Shift+End");
    Check(selection() == Range{0, length}, "Shift+End did not select to the end");
    press(VK_END, false, false); clean("End");
    Check(selection() == Range{length, length}, "End did not move to the end");
    press('A', true, false); clean("Ctrl+A");
    Check(selection() == Range{0, length}, "Ctrl+A did not select the name");
    // Ctrl+Backspace deletes the word before the caret (the reported bug).
    SendMessageW(editor, EM_SETSEL, length, length);
    press(VK_BACK, true, false);
    const std::wstring once = clean("Ctrl+Backspace");
    Check(once == L"PhotoCraft Notes " || once == L"PhotoCraft Notes",
        "Ctrl+Backspace did not delete the previous word");
    press(VK_BACK, true, false);
    const std::wstring twice = clean("Ctrl+Backspace twice");
    Check(twice == L"PhotoCraft " || twice == L"PhotoCraft",
        "a second Ctrl+Backspace did not delete the next word");
    // Plain Backspace and Delete still edit one character; Ctrl+Z undoes it.
    SetWindowTextW(editor, original.c_str());
    SendMessageW(editor, EM_SETSEL, length, length);
    press(VK_BACK, false, false);
    Check(clean("Backspace") == original.substr(0, length - 1), "Backspace did not delete one character");
    press('Z', true, false);
    Check(clean("Ctrl+Z") == original, "Ctrl+Z did not undo the deletion");
    SendMessageW(editor, EM_SETSEL, length, length);
    press(VK_BACK, false, false);
    press(VK_HOME, false, false); press(VK_DELETE, false, false);
    Check(clean("Delete") == original.substr(1, length - 2), "Delete did not delete one character");
    FocusFixture::focus = state.hwnd;
    CancelRename(state);
    std::cout << "PASS: the rename field edits by word and character like Explorer's rename box through "
        "the real message translation path\n";
}

// A click in the field past the shown text ends at the text's end, also where
// GDI advances run wider than the list's layout, with Explorer's EDIT given the
// real pointer position (GetCursorPos/GetMessagePos) as well as the message's:
// a real pointer is where the user sees the text, not where GDI lays it out.
static void TestRenameClickEnd(const std::wstring& fixture) {
    using namespace ShellRenameFixture;
    const auto root = std::filesystem::path(fixture) / L"rename-click-end";
    CreateFiles(root, {L"Quarterly report draft for the regional board meeting.txt"});
    auto values = OptionCatalog::Defaults();
    struct RestoreSettings { ~RestoreSettings() { SettingsFixture::values = nullptr; } } restoreSettings;
    SettingsFixture::values = &values;
    ProviderSelectionFixture::Browser host;
    auto& state = host.state;
    FocusFixture::Session focus(host.frame, state.hwnd);
    host.Navigate(root.wstring(), 1);
    const auto factoryBefore = g_writeFactory;
    struct RestoreFactory { ComPtr<IDWriteFactory> value; ~RestoreFactory() { g_writeFactory = value; } } restoreFactory{factoryBefore};
    if (!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))), "rename click fonts");
    const auto manifest = std::filesystem::path(__FILE__).parent_path() / L"rename-native-controls.manifest";
    ACTCTXW settings{sizeof(settings)}; settings.lpSource = manifest.c_str();
    const HANDLE context = CreateActCtxW(&settings);
    Check(context != INVALID_HANDLE_VALUE, "themed native controls");
    struct ReleaseContext { HANDLE value; ~ReleaseContext() { ReleaseActCtx(value); } } releaseContext{context};
    std::vector<std::pair<void**, void*>> patched;
    struct Unpatch { std::vector<std::pair<void**, void*>>& slots; ~Unpatch() { RenameTypingFixture::Restore(slots); } } unpatch{patched};
    bool drifted = false, failed = false;
    for (const wchar_t* family : {L"Calibri", L"Segoe UI", L"Verdana", L"Arial"}) for (const wchar_t* size : {L"100", L"52"}) {
        values[L"Appearance.fontFamily"] = family;
        values[L"Appearance.fontSizePercent"] = size;
        LoadSettings(); UpdateMetrics(state);
        Check(MakeTextFormats(state) && state.textFormat, "rename click list format");
        state.selectedFlags.Fill(0); state.selectedFlags[0] = 1; state.selected = state.anchor = 0;
        Check(SyncNativeSelection(state), "rename click selection");
        ULONG_PTR cookie = 0;
        Check(ActivateActCtx(context, &cookie), "themed native controls activation");
        BeginRename(state, 0);
        DeactivateActCtx(0, cookie);
        const HWND editor = state.rename.editor;
        Check(editor != nullptr, "rename click editor");
        if (patched.empty()) {
            HMODULE editModule = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(GetClassLongPtrW(editor, GCLP_WNDPROC)), &editModule);
            patched = RenameTypingFixture::PatchImports(editModule,
                {{"GetCursorPos", reinterpret_cast<void*>(&RenameTypingFixture::CursorAt)},
                 {"GetMessagePos", reinterpret_cast<void*>(&RenameTypingFixture::MessageAt)}});
            Check(patched.size() >= 2, "COMCTL32 GetCursorPos/GetMessagePos imports");
        }
        const int length = GetWindowTextLengthW(editor);
        if (SendMessageW(editor, EM_GETLINECOUNT, 0, 0) != 1 || !BuildRenameTextLines(state, editor)) {
            CancelRename(state); continue;
        }
        const auto& last = state.rename.textLines.back();
        const LONG shownEnd = LONG(std::lround(RenameDisplayedX(last, length))), gdiEnd = last.gdiLeft + last.gdiWidth;
        if (gdiEnd < shownEnd + 4) { CancelRename(state); continue; }
        drifted = true;
        ::SetFocus(editor); FocusFixture::focus = editor;
        const LONG y = last.top + state.rename.textLineHeight / 2;
        for (LONG x : {shownEnd + 2, (shownEnd + gdiEnd) / 2, gdiEnd - 1}) {
            SendMessageW(editor, EM_SETSEL, 0, 0);
            RenameTypingFixture::cursor = {x, y};
            ClientToScreen(editor, &RenameTypingFixture::cursor);
            DWORD pressed = 0, start = 0, end = 0; POINT caret{};
            SendMessageW(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            SendMessageW(editor, EM_GETSEL, 0, reinterpret_cast<LPARAM>(&pressed));
            SendMessageW(editor, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            GetCaretPos(&caret);
            std::wcout << L"CLICK-END family=" << family << L" size=" << size << L" shown end=" << shownEnd
                << L" GDI end=" << gdiEnd << L" x=" << x << L" pressed=" << pressed << L" released=" << start << L"-" << end
                << L" of " << length << L" caret=" << caret.x << std::endl;
            failed |= start != DWORD(length) || end != DWORD(length) || std::abs(caret.x - shownEnd) > 1;
        }
        FocusFixture::focus = state.hwnd;
        CancelRename(state);
    }
    Check(drifted, "no tested family's GDI advances run wider than the list's layout");
    Check(!failed, "a click past the shown text did not leave the caret at its end");
    std::cout << "PASS: a click past the shown text, with the pointer there, leaves the caret at the text's end where "
        "GDI advances run wider than the list's layout\n";
}

// The field draws into the Explorer frame's surface beneath the GPU visual,
// which paints no GDI. Closing or hiding the field puts back what that
// surface held, so no field text or border shows through the list afterwards.
static void TestRenameVacatedSurface(const std::wstring& fixture) {
    using namespace ShellRenameFixture;
    // A window surface exists only on screen: place the frame there on a
    // private desktop.
    wchar_t desktop[256]{}; DWORD length = 0;
    Check(GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, desktop, sizeof(desktop), &length),
        "vacated surface desktop identity");
    if (!std::wstring_view(desktop).starts_with(L"gpu-overlay-vacated-surface-")) {
        PrivateDesktopFixture::Run(L"vacated-surface", L"--test rename-vacated-surface", 600000);
        return;
    }
    const auto root = std::filesystem::path(fixture) / L"rename-vacated";
    CreateFiles(root, {L"Quarterly report draft for the regional board meeting.txt"});
    ProviderSelectionFixture::Browser host(true);
    SetWindowPos(host.frame, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    auto& state = host.state;
    FocusFixture::Session focus(host.frame, state.hwnd);
    host.Navigate(root.wstring(), 1);
    // Explorer's frame clips its children; the overlay draws only through
    // DirectComposition and the native view is suppressed.
    SetWindowLongPtrW(host.frame, GWL_STYLE, GetWindowLongPtrW(host.frame, GWL_STYLE) | WS_CLIPCHILDREN);
    Check(SetWindowSubclass(state.hwnd, +[](HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)->LRESULT {
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT) { ValidateRect(hwnd, nullptr); return 0; }
        return DefSubclassProc(hwnd, message, wp, lp);
    }, 2, 0), "GPU overlay paint stand-in");
    Check(SuppressNativeContent(state), "vacated surface native suppression");
    const auto factoryBefore = g_writeFactory;
    struct RestoreFactory { ComPtr<IDWriteFactory> value; ~RestoreFactory() { g_writeFactory = value; } } restoreFactory{factoryBefore};
    if (!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))), "vacated surface fonts");
    Check(MakeTextFormats(state) && state.textFormat, "vacated surface list format");
    // A known surface under the view, read back through the frame's DC. The
    // fixture's own Shell windows may repaint it after a close, so each field
    // starts from it again.
    const COLORREF ground = RGB(23, 117, 61);
    const RECT view = RenameViewport(state, true);
    const auto paintGround = [&] {
        const HDC dc = GetDCEx(host.frame, nullptr, DCX_CACHE);
        const HBRUSH brush = CreateSolidBrush(ground);
        FillRect(dc, &view, brush);
        DeleteObject(brush); ReleaseDC(host.frame, dc);
    };
    const auto foreign = [&](const RECT& area) {
        const int w = area.right - area.left, h = area.bottom - area.top;
        const HDC dc = GetDCEx(host.frame, nullptr, DCX_CACHE);
        BITMAPINFO info{}; info.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
        void* bits = nullptr;
        const HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        const HDC memory = CreateCompatibleDC(dc);
        const HGDIOBJ previous = SelectObject(memory, bitmap);
        BitBlt(memory, 0, 0, w, h, dc, area.left, area.top, SRCCOPY);
        GdiFlush();
        const DWORD expected = (DWORD(GetRValue(ground)) << 16) | (DWORD(GetGValue(ground)) << 8) | GetBValue(ground);
        size_t count = 0;
        for (int i = 0; bits && i < w * h; ++i) count += (static_cast<const DWORD*>(bits)[i] & 0xFFFFFF) != expected;
        SelectObject(memory, previous); DeleteDC(memory); DeleteObject(bitmap); ReleaseDC(host.frame, dc);
        return count;
    };
    const auto open = [&] {
        paintGround();
        Check(foreign(view) == 0, "the frame surface under the view could not be read back");
        state.selectedFlags.Fill(0); state.selectedFlags[0] = 1; state.selected = state.anchor = 0;
        Check(SyncNativeSelection(state), "vacated surface selection");
        BeginRename(state, 0);
        Check(state.rename.editor != nullptr, "vacated surface editor");
        RedrawWindow(state.rename.editor, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
        const RECT field = state.rename.frameRect;
        const size_t drawn = foreign(field);
        Check(drawn > 0, "the field did not draw into the frame surface");
        return std::pair{field, drawn};
    };
    // Closing.
    auto [closed, drawn] = open();
    CancelRename(state);
    const size_t afterClose = foreign(closed);
    // Hiding while the view zooms.
    auto [hidden, drawnAgain] = open();
    state.zoomAnimating = true;
    RepositionRenameEdit(state);
    const size_t afterHide = foreign(hidden);
    state.zoomAnimating = false;
    CancelRename(state);
    std::wcout << L"VACATED field pixels drawn=" << drawn << L" after close=" << afterClose
        << L" drawn=" << drawnAgain << L" after hide=" << afterHide << std::endl;
    Check(afterClose == 0, "the closed field's pixels stayed in the frame surface");
    Check(afterHide == 0, "the hidden field's pixels stayed in the frame surface");
    std::cout << "PASS: closing or hiding the rename field restores the frame surface beneath the GPU view\n";
}
