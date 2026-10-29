// Real Shell menu loops on a private desktop. No menu/site replacements,
// synthesized input messages, desktop switching or user's focus/clipboard.
namespace NativeMenuFixture {
struct Item {
    std::wstring text;
    UINT type = 0, state = 0;
    bool submenu = false;
    bool operator==(const Item&) const = default;
};
std::vector<Item> Read(HMENU menu, bool& valid) {
    std::vector<Item> result;
    const int count = GetMenuItemCount(menu);
    if (count < 0 || count > 512) { valid = false; return result; }
    for (int i = 0; i < count; ++i) {
        wchar_t text[1024]{};
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_STRING | MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU;
        info.dwTypeData = text; info.cch = ARRAYSIZE(text);
        if (!GetMenuItemInfoW(menu, i, TRUE, &info)) { valid = false; break; }
        // Unopened submenus contain provider placeholders until Windows sends
        // WM_INITMENUPOPUP. Compare only the actually displayed root here;
        // expanded submenu interaction belongs to the live mouse check.
        Item item{text, info.fType, info.fState & (MFS_DISABLED | MFS_CHECKED | MFS_DEFAULT), info.hSubMenu != nullptr};
        result.push_back(std::move(item));
    }
    return result;
}
inline thread_local struct Popup* observing = nullptr;
struct Popup {
    unsigned opened = 0;
    bool expired = false, dismissed = false, valid = true;
    RECT bounds{};
    std::vector<Item> items;
    HWND native, frame;
    UINT_PTR timer = 0;
    static LRESULT CALLBACK Owner(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
        // WM_ENTERIDLE comes from Windows' actual menu loop, after a popup is
        // visible. A call or success HRESULT alone is never counted as a menu.
        if (message == WM_ENTERIDLE && wp == MSGF_MENU && observing && lp) {
            const HWND window = reinterpret_cast<HWND>(lp);
            wchar_t type[32]{}; MENUBARINFO bar{sizeof(bar)};
            if (GetClassNameW(window, type, ARRAYSIZE(type)) && !wcscmp(type, L"#32768") &&
                IsWindowVisible(window) && GetMenuBarInfo(window, OBJID_CLIENT, 0, &bar) && bar.hMenu) {
                ++observing->opened;
                GetWindowRect(window, &observing->bounds);
                observing->items = Read(bar.hMenu, observing->valid);
                observing->dismissed = EndMenu() != FALSE;
            }
        }
        return DefSubclassProc(hwnd, message, wp, lp);
    }
    static void CALLBACK Timeout(HWND, UINT, UINT_PTR, DWORD) {
        if (observing) { observing->expired = true; EndMenu(); }
    }
    explicit Popup(HWND native, HWND frame) : native(native), frame(frame) {
        Check(!observing && !MenuFixture::owner, "real popup test must not substitute menu presentation");
        Check(SetWindowSubclass(native, Owner, 273, 0) && SetWindowSubclass(frame, Owner, 273, 0),
            "native menu observation hooks");
        timer = SetTimer(native, 0, 10000, Timeout); // Failure watchdog, never evidence of success.
        Check(timer != 0, "native popup watchdog");
        observing = this;
    }
    ~Popup() {
        KillTimer(native, timer); RemoveWindowSubclass(native, Owner, 273);
        RemoveWindowSubclass(frame, Owner, 273); observing = nullptr;
    }
    bool Displayed() const {
        return opened == 1 && !expired && dismissed && valid && !items.empty() &&
            bounds.right > bounds.left && bounds.bottom > bounds.top;
    }
};
void Child(const std::wstring& fixture) {
    wchar_t desktop[256]{}; DWORD length = 0;
    Check(GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME,
        desktop, sizeof(desktop), &length) && std::wstring(desktop).starts_with(L"gpu-overlay-menu-"),
        "real popup tests must run on their private desktop");
    const auto root = std::filesystem::path(fixture) / L"native-menus";
    std::filesystem::create_directory(root);
    std::filesystem::create_directory(root / L"folder");
    HANDLE file = CreateFileW((root / L"file.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "native menu private file"); CloseHandle(file);
    const auto archive = std::filesystem::path(fixture) / L"menus.zip";
    ProviderSelectionFixture::WriteZip(archive, ProviderSelectionFixture::ZipBytes({
        {"file.txt", "private menu fixture"}, {"folder/", ""}}));
    const auto disk = std::filesystem::path(fixture) / L"menu-target";
    const auto alias = std::filesystem::path(fixture) / L"menu-alias";
    ProviderSelectionFixture::DiskAndAlias(disk, alias);
    unsigned trials = 0;
    for (const auto& location : {root, archive, alias}) {
    ProviderSelectionFixture::Browser host;
    auto& state = host.state;
    SetWindowLongPtrW(host.frame, GWL_EXSTYLE, GetWindowLongPtrW(host.frame, GWL_EXSTYLE) & ~WS_EX_NOACTIVATE);
    SetWindowPos(host.frame, nullptr, 0, 0, 1000, 700, SWP_NOZORDER | SWP_SHOWWINDOW);
    SetForegroundWindow(host.frame); // Only this isolated desktop is affected.
    host.Navigate(location.wstring(), location == alias ? 99 : 2);
    int fileRow = -1, folderRow = -1;
    for (int i = 0; i < state.total; ++i) {
        if (state.entries[i].folder) folderRow = i;
        else fileRow = i;
    }
    Check(fileRow >= 0 && folderRow >= 0, "menu provider fixture needs both a file and folder");
    ComPtr<IShellView> native;
    Check(SUCCEEDED(state.shellBrowser->QueryActiveShellView(&native)), "real popup native view");
    for (auto mode : {FVM_DETAILS, FVM_LIST, FVM_ICON}) {
        Check(SUCCEEDED(state.folderView->SetViewModeAndIconSize(mode, 48)), "real popup view mode");
        host.Pump();
        state.gridView = mode == FVM_ICON; state.columnsView = mode == FVM_DETAILS;
        state.layoutNativeMode = mode; UpdateMetrics(state);
        // Single file, single folder, mixed selection, then folder background.
        for (int target : {0, 1, 2, -1}) {
            if (target < 0) ClearSelectionForFolderMenu(state);
            else if (target == 2) {
                state.selectedFlags.Fill(0);
                state.selectedFlags[fileRow] = state.selectedFlags[folderRow] = 1;
                state.selected = state.anchor = folderRow;
                Check(SyncNativeSelection(state), "real popup multi-selection");
            } else Select(state, target == 0 ? fileRow : folderRow);
            Check(!state.pendingNativeSelection, "small popup selection unexpectedly deferred");
            const POINT point{300, 400};
            // Reference is an actual native popup, not a separately constructed
            // IContextMenu/HMENU. This includes view-owned Refresh/Group by.
            RestoreNativeContent(state);
            std::vector<Item> reference;
            {
                Popup popup(state.nativeHwnd, host.frame);
                NativeContextMenu(state, point, false);
                Check(popup.Displayed(), "visible native view handler did not open its reference popup");
                reference = popup.items;
            }
            Check(SuppressNativeContent(state), "restore suppression after native reference menu");
            SetPresentationVisibility(state.hwnd, true);
            Check(!IsWindowVisible(state.nativeHwnd), "real menu test did not suppress the native view");
            {
                Popup popup(state.nativeHwnd, host.frame);
                if (target < 0) BackgroundContextMenu(state, point);
                else ContextMenu(state, target == 0 ? fileRow : folderRow, point);
                std::cout << "ACTUAL MENU mode=" << mode << " target=" << target << " opened=" << popup.opened
                    << " expired=" << popup.expired << " items=" << popup.items.size() << std::endl;
                Check(popup.Displayed(), "right click did not display a real native popup with the native host suppressed");
                if (popup.items != reference) {
                    for (const auto& item : reference) std::wcout << L"REFERENCE " << item.text << L" state=" << item.state << L'\n';
                    for (const auto& item : popup.items) std::wcout << L"HIDDEN " << item.text << L" state=" << item.state << L'\n';
                }
                Check(popup.items == reference, "displayed popup commands/submenus/states differ with the native host hidden");
            }
            Check(!IsWindowVisible(state.nativeHwnd), "popup exposed the suppressed native list");
            int selected = -1;
            Check(SUCCEEDED(state.folderView->ItemCount(SVGIO_SELECTION, &selected)) &&
                selected == (target < 0 ? 0 : target == 2 ? 2 : 1), "popup changed the native selection");
            ++trials;
        }
    }
    }
    Check(SetFileAttributesW(alias.c_str(), FILE_ATTRIBUTE_NORMAL), "restore private delegated directory attributes");
    std::cout << "PASS: " << trials << " displayed item and background popups; roots match visible native commands/states; native hosts stay hidden; private desktop\n";
}
}
static void TestNativeMenuInteraction() {
    const std::wstring name = L"gpu-overlay-menu-" + std::to_wstring(GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    Check(desktop != nullptr, "private menu desktop creation");
    wchar_t station[256]{}; DWORD length = 0;
    Check(GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, station, sizeof(station), &length),
        "private menu window station");
    std::wstring destination = std::wstring(station) + L"\\" + name;
    std::wstring module(32768, L'\0');
    module.resize(GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size())));
    std::wstring command = L"\"" + module + L"\" --native-context-menu-child";
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    Check(CreatePipe(&read, &write, &attributes, 65536) && SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0),
        "private menu diagnostics pipe");
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &attributes, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW startup{sizeof(startup)}; startup.lpDesktop = destination.data();
    startup.dwFlags = STARTF_USESTDHANDLES; startup.hStdOutput = startup.hStdError = write; startup.hStdInput = input;
    PROCESS_INFORMATION child{};
    const BOOL created = CreateProcessW(module.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child);
    CloseHandle(write); if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (!created) { CloseHandle(read); CloseDesktop(desktop); Check(false, "private menu child creation"); }
    const DWORD waited = WaitForSingleObject(child.hProcess, 60000);
    DWORD result = DWORD(-1);
    if (waited == WAIT_OBJECT_0) GetExitCodeProcess(child.hProcess, &result);
    else TerminateProcess(child.hProcess, 125); // Test process only; never live Explorer/provider threads.
    DWORD available = 0, received = 0;
    char buffer[4096];
    while (PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr) && available &&
        ReadFile(read, buffer, std::min<DWORD>(available, sizeof(buffer)), &received, nullptr) && received)
        std::cout.write(buffer, received);
    CloseHandle(read);
    CloseHandle(child.hThread); CloseHandle(child.hProcess); CloseDesktop(desktop);
    Check(waited == WAIT_OBJECT_0 && result == 0, "actual native menu interaction failed on the isolated desktop");
}
