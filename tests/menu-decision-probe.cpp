// Live diagnostic driver for tests/menu-decision-trace.wh.cpp (not a regression).
//
// Opens a File Explorer window on a private temporary folder and asks its view
// for a context menu in four ways, each a named phase in the trace:
//   top-keyboard   a top-level file selected, keyboard menu request
//   top-mouse      a menu request at that file's row
//   none-keyboard  nothing selected (as with a selection of inline children)
//   none-mouse     a menu request on the view's empty area
// The menu requests go to the view window exactly as the GPU overlay sends them
// (WM_CONTEXTMENU to SHELLDLL_DefView). The trace mod records which calls the
// view makes, with stacks. Each opened menu is dismissed with Escape once
// Explorer shows it. The folder is removed and the window closed at the end.
#include <windows.h>
#include <exdisp.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <uiautomation.h>
#include <cstdio>
#include <string>
#include <algorithm>
#include <functional>
#include <vector>

namespace {
constexpr wchar_t kActiveEvent[] = L"Local\\ExplorerMenuDecisionProbe";
constexpr wchar_t kPhaseMapping[] = L"Local\\ExplorerMenuDecisionProbePhase";
constexpr DWORD kStepLimitMs = 10000; // A bound on each wait for Explorer, never a pacing delay.

template <class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

void Say(const wchar_t* format, ...) {
    SYSTEMTIME now; GetLocalTime(&now);
    wprintf(L"%02u:%02u:%02u.%03u ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    va_list args; va_start(args, format); vwprintf(format, args); va_end(args);
    wprintf(L"\n");
    fflush(stdout);
}

constexpr size_t kPhaseLength = 1024;
wchar_t* g_phase = nullptr;
std::wstring g_forcedItem; // The items the stage-2 override presents, ';'-separated.
bool Present(const std::vector<std::wstring>& items) {
    g_forcedItem.clear();
    for (const auto& item : items) g_forcedItem += (g_forcedItem.empty() ? L"" : L";") + item;
    return true;
}
void Phase(const wchar_t* name) {
    // "phase|file": the override diagnostic reads the file for "forced-*".
    wcsncpy_s(g_phase, kPhaseLength, (std::wstring(name) + L"|" + g_forcedItem).c_str(), _TRUNCATE);
    Say(L"phase %ls", name);
}

// Pumps this thread's messages until the condition holds or the bound passes.
template <class F> bool WaitFor(F condition, DWORD limit = kStepLimitMs) {
    const ULONGLONG end = GetTickCount64() + limit;
    while (!condition()) {
        if (GetTickCount64() >= end) return false;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return true;
}

struct Window {
    Com<IWebBrowser2> browser;
    Com<IShellBrowser> shell;
    Com<IShellView> view;
    Com<IFolderView2> folder;
    HWND frame = nullptr, defView = nullptr;
    DWORD process = 0;
};

// path: the folder shown; or, for a search, "search:" + a marker its location contains.
bool FindWindowFor(const std::wstring& path, Window& window) {
    const bool search = path.rfind(L"search:", 0) == 0;
    Com<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows)))) return false;
    long count = 0;
    windows->get_Count(&count);
    for (long i = 0; i < count; ++i) {
        VARIANT index{}; index.vt = VT_I4; index.lVal = i;
        Com<IDispatch> item;
        if (FAILED(windows->Item(index, &item)) || !item) continue;
        Com<IWebBrowser2> browser;
        if (FAILED(item->QueryInterface(IID_PPV_ARGS(&browser)))) continue;
        Com<IServiceProvider> services;
        Com<IShellBrowser> shell;
        Com<IShellView> view;
        Com<IFolderView2> folder;
        if (FAILED(browser->QueryInterface(IID_PPV_ARGS(&services))) ||
            FAILED(services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&shell))) ||
            FAILED(shell->QueryActiveShellView(&view)) || FAILED(view->QueryInterface(IID_PPV_ARGS(&folder))))
            continue;
        Com<IPersistFolder2> persist;
        PIDLIST_ABSOLUTE id = nullptr;
        wchar_t current[MAX_PATH]{};
        if (FAILED(folder->GetFolder(IID_PPV_ARGS(&persist))) || FAILED(persist->GetCurFolder(&id))) continue;
        PWSTR parsing = nullptr;
        const bool match = search ?
            SUCCEEDED(SHGetNameFromIDList(id, SIGDN_DESKTOPABSOLUTEPARSING, &parsing)) && parsing &&
                !SHGetPathFromIDListW(id, current) && wcsstr(parsing, path.c_str() + 7) : // "Search Results in <folder>&<query>"
            SHGetPathFromIDListW(id, current) && !_wcsicmp(current, path.c_str());
        CoTaskMemFree(parsing);
        CoTaskMemFree(id);
        if (!match) continue;
        SHANDLE_PTR frame = 0;
        browser->get_HWND(&frame);
        window.frame = reinterpret_cast<HWND>(frame);
        view->GetWindow(&window.defView);
        GetWindowThreadProcessId(window.defView, &window.process);
        window.browser.p = browser.p; browser.p = nullptr;
        window.shell.p = shell.p; shell.p = nullptr;
        window.view.p = view.p; view.p = nullptr;
        window.folder.p = folder.p; folder.p = nullptr;
        return true;
    }
    return false;
}

// The row of the item with this full path (search results list items of
// several folders, possibly with the same name).
int ItemIndexByPath(IFolderView2* folder, const std::wstring& path) {
    int count = 0;
    folder->ItemCount(SVGIO_ALLVIEW, &count);
    for (int i = 0; i < count; ++i) {
        Com<IShellItem> item;
        PWSTR file = nullptr;
        if (SUCCEEDED(folder->GetItem(i, IID_PPV_ARGS(&item))) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &file))) {
            const bool match = !_wcsicmp(file, path.c_str());
            CoTaskMemFree(file);
            if (match) return i;
        }
    }
    return -1;
}
int ItemIndex(IFolderView2* folder, const wchar_t* name) {
    int count = 0;
    folder->ItemCount(SVGIO_ALLVIEW, &count);
    for (int i = 0; i < count; ++i) {
        Com<IShellItem> item;
        PWSTR leaf = nullptr;
        if (SUCCEEDED(folder->GetItem(i, IID_PPV_ARGS(&item))) &&
            SUCCEEDED(item->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &leaf))) {
            const bool match = !_wcsicmp(leaf, name);
            CoTaskMemFree(leaf);
            if (match) return i;
        }
    }
    return -1;
}

// The row's screen rectangle, from the view's own UI Automation tree.
bool ItemRect(HWND defView, IFolderView2* folder, int index, RECT& rect) {
    Com<IShellItem> item;
    PWSTR label = nullptr;
    if (FAILED(folder->GetItem(index, IID_PPV_ARGS(&item))) || FAILED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &label)))
        return false;
    Com<IUIAutomation> automation;
    Com<IUIAutomationElement> root, found;
    Com<IUIAutomationCondition> byName;
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(label);
    CoTaskMemFree(label);
    bool ok = SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->ElementFromHandle(defView, &root)) &&
        SUCCEEDED(automation->CreatePropertyCondition(UIA_NamePropertyId, value, &byName)) &&
        SUCCEEDED(root->FindFirst(TreeScope_Descendants, byName.p, &found)) && found &&
        SUCCEEDED(found->get_CurrentBoundingRectangle(&rect)) && rect.right > rect.left;
    VariantClear(&value);
    return ok;
}

// A visible popup of Explorer's process owned by the window: the open menu.
HWND MenuPopup(const Window& window) {
    struct Search { const Window* window; HWND found; } search{&window, nullptr};
    EnumWindows([](HWND hwnd, LPARAM context) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(context);
        DWORD process = 0;
        GetWindowThreadProcessId(hwnd, &process);
        if (process != s.window->process || hwnd == s.window->frame || !IsWindowVisible(hwnd)) return TRUE;
        wchar_t cls[96]{};
        GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
        const bool classic = !wcscmp(cls, L"#32768");
        const bool owned = GetAncestor(hwnd, GA_ROOTOWNER) == s.window->frame;
        if (classic || (owned && (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_POPUP))) { s.found = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

// Each step returns only once it has completely finished, verified against
// Explorer's own state; a step that cannot finish stops the probe (the caller
// reports which), so no later step runs on a state it did not expect.

void Keys(std::initializer_list<WORD> keys) {
    std::vector<INPUT> input;
    for (WORD key : keys) { INPUT down{}; down.type = INPUT_KEYBOARD; down.ki.wVk = key; input.push_back(down); }
    for (auto it = keys.end(); it != keys.begin();) {
        INPUT up{}; up.type = INPUT_KEYBOARD; up.ki.wVk = *--it; up.ki.dwFlags = KEYEVENTF_KEYUP; input.push_back(up);
    }
    SendInput(static_cast<UINT>(input.size()), input.data(), sizeof(INPUT));
}

// No menu of this window is showing (the previous step's menu has closed).
bool NoMenu(const Window& window) {
    if (!WaitFor([&] { return MenuPopup(window) == nullptr; })) { Say(L"  FAILED: a menu is still open"); return false; }
    return true;
}

// The frame is in front and the list (the view or a child of it) has
// keyboard focus, as when the user has clicked into it.
bool FocusList(const Window& window) {
    const DWORD thread = GetWindowThreadProcessId(window.defView, nullptr);
    const auto focused = [&] {
        GUITHREADINFO info{sizeof(info)};
        return GetForegroundWindow() == window.frame && GetGUIThreadInfo(thread, &info) && info.hwndFocus &&
            (info.hwndFocus == window.defView || IsChild(window.defView, info.hwndFocus));
    };
    if (!focused()) {
        SetForegroundWindow(window.frame);
        window.view->UIActivate(SVUIA_ACTIVATE_FOCUS);
    }
    if (!WaitFor(focused)) { Say(L"  FAILED: the list did not take keyboard focus"); return false; }
    return true;
}

bool Select(const Window& window, int index) {
    // Deselect everything first, then (optionally) the one row with focus.
    window.folder->SelectItem(-1, SVSI_DESELECTOTHERS);
    if (index >= 0) window.folder->SelectItem(index, SVSI_SELECT | SVSI_DESELECTOTHERS | SVSI_FOCUSED | SVSI_ENSUREVISIBLE);
    const int expected = index >= 0 ? 1 : 0;
    int selected = -1;
    const bool done = WaitFor([&] {
        if (FAILED(window.folder->ItemCount(SVGIO_SELECTION, &selected)) || selected != expected) return false;
        if (index < 0) return true;
        int first = -1;
        return SUCCEEDED(window.folder->GetSelectedItem(0, &first)) && first == index;
    });
    Say(L"  native selection count=%d", selected);
    if (!done) Say(L"  FAILED: the selection did not become %d item(s)", expected);
    return done;
}

// The names of a menu window's entries, joined; empty until it has any.
std::wstring MenuEntries(HWND popup) {
    Com<IUIAutomation> automation;
    Com<IUIAutomationElement> root;
    Com<IUIAutomationCondition> items;
    Com<IUIAutomationElementArray> found;
    VARIANT type{}; type.vt = VT_I4; type.lVal = UIA_MenuItemControlTypeId;
    std::wstring names;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) ||
        FAILED(automation->ElementFromHandle(popup, &root)) ||
        FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &items)) ||
        FAILED(root->FindAll(TreeScope_Descendants, items.p, &found)) || !found) return names;
    int count = 0;
    found->get_Length(&count);
    for (int i = 0; i < count; ++i) {
        Com<IUIAutomationElement> item;
        BSTR name = nullptr;
        if (SUCCEEDED(found->GetElement(i, &item)) && SUCCEEDED(item->get_CurrentName(&name)) && name) {
            if (!names.empty()) names += L", ";
            names += name;
            SysFreeString(name);
        }
    }
    return names;
}

// Invokes the open menu's entry with this name (UI Automation Invoke).
// Entry names are the system's display language; this diagnostic runs on the
// developer's machine and prints every entry, so a mismatch is visible.
bool InvokeEntry(HWND popup, const wchar_t* name) {
    Com<IUIAutomation> automation;
    Com<IUIAutomationElement> root, entry;
    Com<IUIAutomationCondition> byName;
    Com<IUIAutomationInvokePattern> invoke;
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(name);
    const bool found = SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->ElementFromHandle(popup, &root)) &&
        SUCCEEDED(automation->CreatePropertyCondition(UIA_NamePropertyId, value, &byName)) &&
        SUCCEEDED(root->FindFirst(TreeScope_Descendants, byName.p, &entry)) && entry &&
        SUCCEEDED(entry->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) && invoke;
    VariantClear(&value);
    if (!found) { Say(L"  FAILED: no invokable entry named \"%ls\"", name); return false; }
    Say(L"  invoking \"%ls\"", name);
    return SUCCEEDED(invoke->Invoke());
}

std::wstring ClipboardText() {
    std::wstring text;
    if (!OpenClipboard(nullptr)) return text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
        if (const auto* chars = static_cast<const wchar_t*>(GlobalLock(data))) { text = chars; GlobalUnlock(data); }
    CloseClipboard();
    return text;
}
void ClearClipboard() {
    if (OpenClipboard(nullptr)) { EmptyClipboard(); CloseClipboard(); }
}

// "Copy as path": the clipboard must hold exactly these paths, quoted, one per line.
std::function<bool(HWND)> CopyAsPath(const std::vector<std::wstring>& items) {
    return [items](HWND popup) {
        std::wstring expected;
        for (const auto& item : items) expected += (expected.empty() ? L"\"" : L"\r\n\"") + item + L"\"";
        ClearClipboard();
        if (!InvokeEntry(popup, L"Copy as path")) return false;
        std::wstring copied;
        if (!WaitFor([&] { return !(copied = ClipboardText()).empty(); })) { Say(L"  FAILED: nothing was copied"); return false; }
        Say(L"  clipboard: %ls", copied.c_str());
        // Selection order is the view's; compare the set of lines.
        const auto lines = [](std::wstring text) {
            std::vector<std::wstring> out;
            for (size_t start = 0; start <= text.size();) {
                size_t end = text.find(L"\r\n", start);
                if (end == std::wstring::npos) end = text.size();
                out.push_back(text.substr(start, end - start));
                start = end + 2;
            }
            std::sort(out.begin(), out.end());
            return out;
        };
        if (lines(copied) != lines(expected)) { Say(L"  FAILED: expected %ls", expected.c_str()); return false; }
        Say(L"  copied the expected paths");
        return true;
    };
}

// Removes the item Explorer recycled from this path, if any, so the probe
// leaves nothing behind outside its folder.
bool PurgeRecycled(const std::wstring& path) {
    Com<IShellItem> bin;
    Com<IEnumShellItems> items;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_RecycleBinFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&bin))) ||
        FAILED(bin->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items)))) return false;
    const wchar_t* leaf = PathFindFileNameW(path.c_str());
    const std::wstring folder(path.c_str(), leaf - path.c_str() - 1);
    const PROPERTYKEY deletedFrom{{0x9B174B33, 0x40FF, 0x11D2, {0xA2, 0x7E, 0x00, 0xC0, 0x4F, 0xC3, 0x08, 0x71}}, 2};
    for (Com<IShellItem> item; items->Next(1, &item.p, nullptr) == S_OK; item.p->Release(), item.p = nullptr) {
        Com<IShellItem2> details;
        PWSTR name = nullptr, from = nullptr;
        if (FAILED(item->QueryInterface(IID_PPV_ARGS(&details))) || FAILED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name))) continue;
        const bool match = SUCCEEDED(details->GetString(deletedFrom, &from)) && from && !_wcsicmp(from, folder.c_str()) &&
            (!_wcsicmp(name, leaf) || !_wcsnicmp(name, leaf, wcslen(name)));
        CoTaskMemFree(name); CoTaskMemFree(from);
        if (!match) continue;
        Com<IFileOperation> operation;
        if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation))) ||
            FAILED(operation->SetOperationFlags(FOF_NO_UI)) || FAILED(operation->DeleteItem(item.p, nullptr)) ||
            FAILED(operation->PerformOperations())) return false;
        Say(L"  removed the recycled copy again");
        return true;
    }
    return false;
}

// "Delete" with Shift held: a permanent delete (nothing goes to the Recycle
// Bin), confirmed in Explorer's dialog. The file must disappear from disk.
std::function<bool(HWND)> PermanentDelete(const Window& window, const std::wstring& path) {
    return [&window, path](HWND popup) {
        INPUT shift{}; shift.type = INPUT_KEYBOARD; shift.ki.wVk = VK_SHIFT;
        SendInput(1, &shift, sizeof(INPUT));
        bool ok = InvokeEntry(popup, L"Delete");
        HWND dialog = nullptr;
        const auto confirm = [&] {
            struct Search { DWORD process; HWND found; } search{window.process, nullptr};
            EnumWindows([](HWND hwnd, LPARAM context) -> BOOL {
                auto& s = *reinterpret_cast<Search*>(context);
                DWORD process = 0; GetWindowThreadProcessId(hwnd, &process);
                wchar_t cls[32]{}; GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
                if (process == s.process && IsWindowVisible(hwnd) && !wcscmp(cls, L"#32770")) { s.found = hwnd; return FALSE; }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&search));
            return (dialog = search.found) != nullptr || !PathFileExistsW(path.c_str());
        };
        ok = ok && WaitFor(confirm);
        shift.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &shift, sizeof(INPUT));
        if (!ok) { Say(L"  FAILED: no confirmation and the file is still there"); return false; }
        if (dialog) {
            Say(L"  confirming the permanent delete");
            SetForegroundWindow(dialog);
            Keys({VK_RETURN}); // The dialog's default button deletes.
        }
        if (!WaitFor([&] { return !PathFileExistsW(path.c_str()); })) { Say(L"  FAILED: the file was not deleted"); return false; }
        if (!dialog) {
            Say(L"  NOTE: deleted without a permanent-delete confirmation; checking the Recycle Bin");
            PurgeRecycled(path);
        }
        Say(L"  the file was deleted");
        return true;
    };
}

// "Rename": Explorer starts editing the item's name (an edit field appears in
// the view); Escape cancels it, so nothing is renamed.
std::function<bool(HWND)> StartRename(const Window& window, const std::wstring& path) {
    return [&window, path](HWND popup) {
        if (!InvokeEntry(popup, L"Rename")) return false;
        HWND edit = nullptr;
        const bool editing = WaitFor([&] {
            GUITHREADINFO info{}; info.cbSize = sizeof(info);
            wchar_t cls[32]{};
            if (GetGUIThreadInfo(GetWindowThreadProcessId(window.defView, nullptr), &info) && info.hwndFocus &&
                GetClassNameW(info.hwndFocus, cls, ARRAYSIZE(cls)) && !_wcsicmp(cls, L"Edit")) edit = info.hwndFocus;
            return edit != nullptr;
        });
        if (!editing) { Say(L"  RESULT: no rename field appeared (Explorer cannot edit an item it does not show)"); return true; }
        wchar_t text[MAX_PATH]{};
        GetWindowTextW(edit, text, ARRAYSIZE(text));
        Say(L"  RESULT: a rename field appeared with \"%ls\"; cancelling", text);
        Keys({VK_ESCAPE});
        WaitFor([&] { return !IsWindowVisible(edit); });
        if (!PathFileExistsW(path.c_str())) { Say(L"  FAILED: the file was renamed"); return false; }
        return true;
    };
}

// point == -1: Shift+F10 in the focused list (the keyboard request a user
// makes). Otherwise WM_CONTEXTMENU at that screen point, as the GPU overlay
// sends it. Returns once the menu has opened and closed again.
bool RequestMenu(const Window& window, const wchar_t* phase, LPARAM point,
                 const std::function<bool(HWND)>& action = {}) {
    if (!NoMenu(window)) return false;
    if (point == -1 && !FocusList(window)) return false;
    Phase(phase);
    if (point == -1) Keys({VK_SHIFT, VK_F10});
    else PostMessageW(window.defView, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(window.defView), point);
    HWND popup = nullptr;
    if (!WaitFor([&] { return (popup = MenuPopup(window)) != nullptr; })) {
        Say(L"  FAILED: no menu window appeared; Explorer's visible windows:");
        EnumWindows([](HWND hwnd, LPARAM context) -> BOOL {
            const auto& w = *reinterpret_cast<const Window*>(context);
            DWORD process = 0;
            GetWindowThreadProcessId(hwnd, &process);
            if (process != w.process || !IsWindowVisible(hwnd)) return TRUE;
            wchar_t cls[96]{};
            GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
            RECT r{}; GetWindowRect(hwnd, &r);
            Say(L"    %p class=%ls owner=%p rootowner=%p style=0x%08lX rect=%ld,%ld,%ld,%ld fg=%d", hwnd, cls,
                GetWindow(hwnd, GW_OWNER), GetAncestor(hwnd, GA_ROOTOWNER),
                static_cast<unsigned long>(GetWindowLongPtrW(hwnd, GWL_STYLE)), r.left, r.top, r.right, r.bottom,
                GetForegroundWindow() == hwnd);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&window));
        return false;
    }
    wchar_t cls[96]{};
    GetClassNameW(popup, cls, ARRAYSIZE(cls));
    Say(L"  menu window %p class=%ls", popup, cls);
    // Which menu opened: its entries, as UI Automation exposes them.
    std::wstring entries;
    if (!WaitFor([&] { return !(entries = MenuEntries(popup)).empty(); })) {
        Say(L"  FAILED: the menu exposed no entries");
        return false;
    }
    Say(L"  entries: %ls", entries.c_str());
    // An action picks an entry (which closes the menu) and waits for its
    // effect while the step is still current. Otherwise Escape closes either
    // menu kind (the menu owns input while it is shown).
    if (action) {
        if (!action(popup)) { Keys({VK_ESCAPE}); Phase(L"idle"); return false; }
    } else Keys({VK_ESCAPE});
    if (!WaitFor([&] { return !IsWindowVisible(popup) || !IsWindow(popup); })) {
        Say(L"  FAILED: the menu did not close");
        return false;
    }
    Say(L"  menu closed");
    Phase(L"idle"); // The next step's checks run outside this step.
    return true;
}
}

// A search scoped to the probe's folder, in its own Explorer window: its
// results hold items of several folders. These are selected natively and the
// menu is requested, to compare Explorer's own cross-folder menu (entries,
// Copy as path; the trace logs how its folder builds the menu).
bool SearchStep(const std::wstring& root, const std::vector<std::wstring>& items) {
    std::wstring location;
    for (wchar_t c : root) {
        wchar_t escaped[8];
        if (iswalnum(c) || c == L'-' || c == L'_' || c == L'.') location += c;
        else { swprintf_s(escaped, L"%%%02X", static_cast<unsigned>(c)); location += escaped; }
    }
    const std::wstring query = L"search-ms:query=*.txt&crumb=location:" + location;
    const std::wstring marker = L"search:" + std::wstring(PathFindFileNameW(root.c_str()));
    Phase(L"search-open");
    ShellExecuteW(nullptr, L"open", query.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    Window search;
    if (!WaitFor([&] { return FindWindowFor(marker, search); })) {
        Say(L"  FAILED: the search window did not register");
        return false;
    }
    std::vector<int> rows;
    // The search lists the folder's files as it finds them; wait for these.
    const bool listed = WaitFor([&] {
        rows.clear();
        for (const auto& item : items) {
            const int row = ItemIndexByPath(search.folder.p, item);
            if (row < 0) return false;
            rows.push_back(row);
        }
        return true;
    }, 30000);
    bool ok = listed;
    if (!listed) Say(L"  FAILED: the search did not list the items");
    if (ok) {
        Say(L"  search window frame=%p view=%p", search.frame, search.defView);
        search.folder->SelectItem(-1, SVSI_DESELECTOTHERS);
        for (size_t i = 0; i < rows.size(); ++i)
            search.folder->SelectItem(rows[i], SVSI_SELECT | (i == 0 ? SVSI_FOCUSED | SVSI_DESELECTOTHERS : 0));
        int selected = 0;
        ok = WaitFor([&] { search.folder->ItemCount(SVGIO_SELECTION, &selected); return selected == static_cast<int>(rows.size()); });
        Say(L"  native selection count=%d", selected);
        ok = ok && RequestMenu(search, L"search-native", -1, CopyAsPath(items));
    }
    if (search.browser) search.browser->Quit();
    return ok;
}

// Windows debug output (OutputDebugString, which Wh_Log writes to) of the
// trace and override diagnostics, collected while the probe runs.
struct DebugCapture {
    HANDLE ready = nullptr, data = nullptr, mapping = nullptr, stop = nullptr, thread = nullptr;
    FILE* file = nullptr;
    struct Buffer { DWORD process; char text[4096 - sizeof(DWORD)]; }* buffer = nullptr;
    bool Start(const wchar_t* path) {
        ready = CreateEventW(nullptr, FALSE, FALSE, L"DBWIN_BUFFER_READY");
        data = CreateEventW(nullptr, FALSE, FALSE, L"DBWIN_DATA_READY");
        if (ready && GetLastError() == ERROR_ALREADY_EXISTS) Say(L"note: another debug-output reader is running; it may receive some lines instead");
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Buffer), L"DBWIN_BUFFER");
        buffer = mapping ? static_cast<Buffer*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) : nullptr;
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ready || !data || !buffer || !stop || _wfopen_s(&file, path, L"wb")) return false;
        thread = CreateThread(nullptr, 0, [](void* self) -> DWORD {
            auto& c = *static_cast<DebugCapture*>(self);
            HANDLE waits[2]{c.data, c.stop};
            SetEvent(c.ready);
            while (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0) {
                if (strstr(c.buffer->text, "[MenuTrace") || strstr(c.buffer->text, "[MenuOverride")) {
                    fputs(c.buffer->text, c.file);
                    if (!strchr(c.buffer->text, '\n')) fputc('\n', c.file);
                    fflush(c.file);
                }
                SetEvent(c.ready);
            }
            return 0;
        }, this, 0, nullptr);
        return thread != nullptr;
    }
    ~DebugCapture() {
        if (thread) { SetEvent(stop); WaitForSingleObject(thread, INFINITE); CloseHandle(thread); }
        if (file) fclose(file);
        if (buffer) UnmapViewOfFile(buffer);
        for (HANDLE h : {ready, data, mapping, stop}) if (h) CloseHandle(h);
    }
};

int wmain(int argc, wchar_t** argv) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    DebugCapture capture;
    if (argc > 1) {
        if (capture.Start(argv[1])) Say(L"collecting the diagnostics' output in %ls", argv[1]);
        else Say(L"could not collect debug output; save the Windhawk log instead");
    }
    // Private fixture: a top-level file, a folder, and a file inside it.
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(ARRAYSIZE(temp), temp);
    wchar_t rootBuffer[MAX_PATH]{};
    swprintf_s(rootBuffer, L"%lsmenu-decision-probe-%lu", temp, GetCurrentProcessId());
    // The diagnostics append to this file while the probe's event exists.
    const std::wstring diagnostics = std::wstring(temp) + L"menu-decision-probe.log";
    DeleteFileW(diagnostics.c_str());
    Say(L"diagnostics log: %ls", diagnostics.c_str());
    const std::wstring root = rootBuffer, sub = root + L"\\sub", other = root + L"\\other";
    const std::wstring nested = sub + L"\\nested.txt", second = sub + L"\\second.txt", third = other + L"\\third.txt",
        doomed = sub + L"\\doomed.txt", renamed = sub + L"\\rename-me.txt";
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW(sub.c_str(), nullptr);
    CreateDirectoryW(other.c_str(), nullptr);
    const std::wstring sameName = other + L"\\nested.txt"; // Same name as sub\nested.txt, another folder.
    for (const std::wstring& file : {root + L"\\top.txt", nested, second, third, doomed, renamed, sameName}) {
        HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    HANDLE active = CreateEventW(nullptr, TRUE, FALSE, kActiveEvent);
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kPhaseLength * sizeof(wchar_t), kPhaseMapping);
    g_phase = mapping ? static_cast<wchar_t*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0)) : nullptr;
    int result = 1;
    Window window;
    if (!active || !g_phase) {
        Say(L"could not create the probe's event or phase name");
    } else {
        Phase(L"open");
        ShellExecuteW(nullptr, L"open", root.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (!WaitFor([&] { return FindWindowFor(root, window); })) {
            Say(L"the Explorer window for %ls did not register", root.c_str());
        } else if (!WaitFor([&] { return ItemIndex(window.folder.p, L"top.txt") >= 0; })) {
            Say(L"the folder's items did not appear");
        } else {
            Say(L"window frame=%p view=%p process=%lu", window.frame, window.defView, window.process);
            const int top = ItemIndex(window.folder.p, L"top.txt");
            SetEvent(active);
            RECT row{}, client{};
            const auto rowPoint = [&] {
                return MAKELPARAM((row.left + row.right) / 2, (row.top + row.bottom) / 2);
            };
            const auto emptyPoint = [&] {
                GetClientRect(window.defView, &client);
                POINT empty{client.right - 24, client.bottom - 24};
                ClientToScreen(window.defView, &empty);
                return MAKELPARAM(empty.x, empty.y);
            };
            Present({nested});
            const bool finished =
                FocusList(window) && Select(window, top) && RequestMenu(window, L"top-keyboard", -1) &&
                Select(window, top) && (ItemRect(window.defView, window.folder.p, top, row) ||
                    (Say(L"  FAILED: the row has no UI Automation rectangle"), false)) &&
                RequestMenu(window, L"top-mouse", rowPoint()) &&
                Select(window, -1) && RequestMenu(window, L"none-keyboard", -1) &&
                Select(window, -1) && RequestMenu(window, L"none-mouse", emptyPoint()) &&
                // Stage 2: with tests/menu-decision-override.wh.cpp enabled, the
                // same requests open the item menu of the file in the subfolder.
                Select(window, -1) && RequestMenu(window, L"forced-keyboard", -1) &&
                Select(window, -1) && RequestMenu(window, L"forced-mouse", emptyPoint()) &&
                // Only the redirect and _GetItems (no public selection overrides).
                Select(window, -1) && RequestMenu(window, L"minimal-keyboard", -1) &&
                // Commands from the forced menu act on the presented items only.
                Present({nested}) && Select(window, -1) &&
                RequestMenu(window, L"forced-copy-one", -1, CopyAsPath({nested})) &&
                Present({doomed}) && Select(window, -1) &&
                RequestMenu(window, L"forced-delete", -1, PermanentDelete(window, doomed)) &&
                Present({renamed}) && Select(window, -1) &&
                RequestMenu(window, L"forced-rename", -1, StartRename(window, renamed)) &&
                Present({nested, sameName, third}) && Select(window, -1) &&
                RequestMenu(window, L"forced-copy-two-folders", -1, CopyAsPath({nested, sameName, third})) &&
                Present({nested, second}) && Select(window, -1) &&
                RequestMenu(window, L"forced-copy-same-folder", -1, CopyAsPath({nested, second})) &&
                // Items of different folders: classic menu in the GPU mod (the modern
                // presenter prepared them but no menu window was seen; see the doc).
                // Repetition: menus right after a multi-item command once showed
                // no window. Alternate them to see whether that recurs.
                [&] {
                    for (int round = 1; round <= 8; ++round) {
                        Say(L"  round %d", round);
                        if (!(Present({nested, sameName, third}) && Select(window, -1) &&
                              RequestMenu(window, L"forced-repeat-multi", -1, CopyAsPath({nested, sameName, third})) &&
                              Present({second}) && Select(window, -1) &&
                              RequestMenu(window, L"forced-repeat-single", -1, CopyAsPath({second}))))
                            return false;
                    }
                    return true;
                }() &&
                // Everything else in the folder is untouched.
                ((PathFileExistsW(nested.c_str()) && PathFileExistsW(second.c_str()) && PathFileExistsW(third.c_str()) &&
                  PathFileExistsW(renamed.c_str()) && PathFileExistsW((root + L"\\top.txt").c_str())) ||
                 (Say(L"  FAILED: a file other than the deleted one changed"), false)) &&
                // Explorer's own search results: items of several folders (two with
                // the same name) selected natively, with no override.
                SearchStep(root, {nested, sameName, third});
            Phase(finished ? L"done" : L"stopped");
            ResetEvent(active);
            result = finished ? 0 : 1;
        }
        if (window.browser) window.browser->Quit();
    }
    if (g_phase) UnmapViewOfFile(g_phase);
    if (mapping) CloseHandle(mapping);
    if (active) CloseHandle(active);
    // Remove the fixture once Explorer has let go of it.
    SHFILEOPSTRUCTW remove{};
    std::wstring from = root + L'\0';
    remove.wFunc = FO_DELETE;
    remove.pFrom = from.c_str();
    remove.fFlags = FOF_NO_UI;
    WaitFor([&] { return SHFileOperationW(&remove) == 0 || !PathFileExistsW(root.c_str()); });
    CoUninitialize();
    return result;
}
